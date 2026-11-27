#include "core/setup_web_server.hpp"

#include "core/rom_catalog.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <utility>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace czx::core {

namespace {

constexpr size_t kMaxHeaderBytes = 64u * 1024u;
constexpr size_t kMaxBodyBytes = 64u * 1024u * 1024u;

void set_cloexec(int fd) {
    const int flags = ::fcntl(fd, F_GETFD, 0);
    if (flags >= 0) {
        (void)::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
}

std::string to_lower_ascii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string trim_ascii(std::string s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) {
        ++a;
    }
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) {
        --b;
    }
    return s.substr(a, b - a);
}

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            const int hi = hex_val(in[i + 1]);
            const int lo = hex_val(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        } else if (in[i] == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(in[i]);
    }
    return out;
}

std::unordered_map<std::string, std::string> parse_query(const std::string& query) {
    std::unordered_map<std::string, std::string> params;
    size_t pos = 0;
    while (pos < query.size()) {
        const size_t amp = query.find('&', pos);
        const std::string pair = (amp == std::string::npos) ? query.substr(pos) : query.substr(pos, amp - pos);
        const size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            params[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
        } else if (!pair.empty()) {
            params[url_decode(pair)] = "";
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return params;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char ch : s) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(ch));
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

const char* tier_to_str(RomTier t) {
    switch (t) {
        case RomTier::RequiredMissing: return "REQUIRED_MISSING";
        case RomTier::RequiredOk: return "REQUIRED_OK";
        case RomTier::BuiltinNoRoms: return "BUILTIN_NO_ROMS";
        case RomTier::BuiltinPlusRoms: return "BUILTIN_PLUS_ROMS";
        case RomTier::OptionalEmpty: return "OPTIONAL_EMPTY";
        case RomTier::OptionalOk: return "OPTIONAL_OK";
    }
    return "BUILTIN_NO_ROMS";
}

bool send_all(int fd, const void* buf, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    size_t sent = 0;
    while (sent < len) {
        const ssize_t n = ::send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

void send_http_response(int fd,
                        int code,
                        const char* status_text,
                        const char* content_type,
                        const std::string& body) {
    std::ostringstream hdr;
    hdr << "HTTP/1.1 " << code << " " << status_text << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Cache-Control: no-store\r\n"
        << "Connection: close\r\n\r\n";
    const std::string h = hdr.str();
    if (send_all(fd, h.data(), h.size()) && !body.empty()) {
        (void)send_all(fd, body.data(), body.size());
    }
}

void send_json(int fd, int code, const char* status_text, const std::string& json) {
    send_http_response(fd, code, status_text, "application/json; charset=utf-8", json);
}

bool extract_multipart_file(const std::string& content_type,
                            const std::vector<uint8_t>& body,
                            std::string& filename_out,
                            std::vector<uint8_t>& file_bytes_out) {
    const std::string ct_low = to_lower_ascii(content_type);
    const size_t bpos = ct_low.find("boundary=");
    if (bpos == std::string::npos) return false;

    std::string boundary = content_type.substr(bpos + 9);
    const size_t semi = boundary.find(';');
    if (semi != std::string::npos) boundary.resize(semi);
    boundary = trim_ascii(boundary);
    if (boundary.size() >= 2 && boundary.front() == '"' && boundary.back() == '"') {
        boundary = boundary.substr(1, boundary.size() - 2);
    }
    if (boundary.empty()) return false;

    const std::string marker = "--" + boundary;
    const std::string end_marker = "\r\n--" + boundary;
    const std::string body_str(reinterpret_cast<const char*>(body.data()), body.size());

    size_t part_start = body_str.find(marker);
    while (part_start != std::string::npos) {
        part_start += marker.size();
        if (part_start + 2 <= body_str.size() && body_str[part_start] == '-' && body_str[part_start + 1] == '-') {
            break;
        }
        if (part_start + 2 <= body_str.size() && body_str[part_start] == '\r' && body_str[part_start + 1] == '\n') {
            part_start += 2;
        }
        const size_t hdr_end = body_str.find("\r\n\r\n", part_start);
        if (hdr_end == std::string::npos) break;

        const std::string part_hdrs = body_str.substr(part_start, hdr_end - part_start);
        const size_t data_start = hdr_end + 4;
        const size_t data_end = body_str.find(end_marker, data_start);
        if (data_end == std::string::npos) break;

        const size_t fn_pos = part_hdrs.find("filename=\"");
        if (fn_pos != std::string::npos) {
            const size_t fn_val_start = fn_pos + 10;
            const size_t fn_val_end = part_hdrs.find('"', fn_val_start);
            if (fn_val_end != std::string::npos) {
                filename_out = part_hdrs.substr(fn_val_start, fn_val_end - fn_val_start);
            }
            file_bytes_out.assign(body.begin() + static_cast<std::ptrdiff_t>(data_start),
                                  body.begin() + static_cast<std::ptrdiff_t>(data_end));
            return true;
        }
        part_start = data_end + 2;
    }
    return false;
}

std::string build_status_json(const std::string& data_dir) {
    const auto& docs = get_machine_docs();
    const auto statuses = inspect_machines(data_dir);

    std::ostringstream js;
    js << "{\"dataDir\":\"" << json_escape(data_dir) << "\",\"machines\":[";
    for (size_t i = 0; i < docs.size(); ++i) {
        if (i > 0) js << ',';
        const MachineDoc& m = docs[i];
        const MachineStatus& st = statuses[i];
        const auto media = list_media_files(data_dir, m.id);

        js << '{'
           << "\"id\":\"" << json_escape(m.id) << "\","
           << "\"name\":\"" << json_escape(m.name) << "\","
           << "\"subtitle\":\"" << json_escape(m.subtitle) << "\","
           << "\"romsSubDir\":\"" << json_escape(m.roms_sub_dir) << "\","
           << "\"caseRule\":\"" << json_escape(m.case_rule) << "\","
           << "\"builtinTag\":" << (m.builtin_tag ? ("\"" + json_escape(m.builtin_tag) + "\"") : "null") << ','
           << "\"mediaDir\":\"" << json_escape(m.media_dir) << "\","
           << "\"mediaExts\":\"" << json_escape(m.media_exts) << "\","
           << "\"tip1\":\"" << json_escape(m.tip1 ? m.tip1 : "") << "\","
           << "\"tip2\":\"" << json_escape(m.tip2 ? m.tip2 : "") << "\","
           << "\"tier\":\"" << tier_to_str(st.tier) << "\","
           << "\"reqFound\":" << st.req_found << ','
           << "\"reqTotal\":" << st.req_total << ','
           << "\"optFound\":" << st.opt_found << ','
           << "\"optTotal\":" << st.opt_total << ','
           << "\"slots\":[";

        for (size_t s = 0; s < m.slots.size(); ++s) {
            if (s > 0) js << ',';
            const RomSlotSpec& sl = m.slots[s];
            const RomSlotStatus& ss = st.slots[s];
            bool verified = false;
            std::string verified_label;
            if (ss.installed) {
                std::filesystem::path full = std::filesystem::path(data_dir) / "roms" / m.roms_sub_dir;
                if (sl.rel_dir && sl.rel_dir[0] != '\0') {
                    full /= sl.rel_dir;
                }
                full /= ss.filename;
                if (const KnownRomEntry* hit = identify_rom_file_by_hash(full.string(), m.id)) {
                    if (std::string(hit->slot_id) == sl.id) {
                        verified = true;
                        verified_label = hit->label;
                    }
                }
            }
            js << '{'
               << "\"id\":\"" << json_escape(sl.id) << "\","
               << "\"canonical\":\"" << json_escape(sl.canonical) << "\","
               << "\"relDir\":\"" << json_escape(sl.rel_dir) << "\","
               << "\"required\":" << (sl.required ? "true" : "false") << ','
               << "\"caseInsensitive\":" << (sl.case_insensitive ? "true" : "false") << ','
               << "\"note\":\"" << json_escape(sl.note ? sl.note : "") << "\","
               << "\"installed\":" << (ss.installed ? "true" : "false") << ','
               << "\"filename\":\"" << json_escape(ss.filename) << "\","
               << "\"sizeBytes\":" << ss.size_bytes << ','
               << "\"verified\":" << (verified ? "true" : "false") << ','
               << "\"verifiedLabel\":\"" << json_escape(verified_label) << "\""
               << '}';
        }
        js << "],\"media\":[";
        for (size_t f = 0; f < media.size(); ++f) {
            if (f > 0) js << ',';
            js << '{'
               << "\"name\":\"" << json_escape(media[f].name) << "\","
               << "\"sizeBytes\":" << media[f].size_bytes
               << '}';
        }
        js << "]}";
    }
    js << "]}";
    return js.str();
}

const char kWebUiHtml[] = R"CZXHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CZX Setup — ROM &amp; Media Uploader</title>
<style>
  :root{
    --bg:#0e121a;--card:#161e2c;--card2:#1c2739;--bd:#2c3d57;
    --fg:#e4ebf5;--muted:#90a4bf;--acc:#5ed3ff;--ok:#5be691;--warn:#ffbe55;--err:#ff6b7a;
  }
  *{box-sizing:border-box}
  body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg);line-height:1.45}
  header{background:#121925;border-bottom:1px solid var(--bd);padding:14px 18px;display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}
  header h1{margin:0;font-size:18px;color:var(--acc)}
  header .sub{font-size:12px;color:var(--muted)}
  main{max-width:1100px;margin:0 auto;padding:16px;display:grid;gap:16px}
  .btn{background:#233852;color:var(--fg);border:1px solid #3c5d85;border-radius:6px;padding:7px 12px;font-size:13px;cursor:pointer}
  .btn:hover{background:#2c4869}
  .btn.pri{background:#1d5f86;border-color:var(--acc);color:#fff}
  .btn.danger{background:#52222c;border-color:#9e4252}
  .dropzone{background:var(--card);border:2px dashed #3d5d85;border-radius:10px;padding:18px;text-align:center;transition:border-color .15s,background .15s}
  .dropzone.drag{border-color:var(--acc);background:#18283e}
  .dropzone h2{margin:0 0 6px;font-size:15px}
  .dropzone p{margin:0 0 12px;color:var(--muted);font-size:13px}
  .row{display:flex;gap:10px;align-items:center;justify-content:center;flex-wrap:wrap}
  select{background:var(--card2);color:var(--fg);border:1px solid var(--bd);border-radius:6px;padding:7px 10px;font-size:13px}
  .log{background:#111722;border:1px solid var(--bd);border-radius:8px;padding:10px 12px;font-size:13px;display:none;white-space:pre-wrap}
  .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(215px,1fr));gap:10px}
  .mcard{background:var(--card);border:1px solid var(--bd);border-radius:8px;padding:11px 12px;cursor:pointer;display:flex;flex-direction:column;gap:4px}
  .mcard:hover{border-color:var(--acc)}
  .mcard.active{border-color:var(--acc);background:var(--card2)}
  .mtop{display:flex;justify-content:space-between;align-items:center;gap:6px}
  .mname{font-weight:600;font-size:14px}
  .badge{font-size:11px;font-weight:600;padding:2px 7px;border-radius:999px;background:#263449;color:var(--muted)}
  .badge.ok{background:#163d28;color:var(--ok)}
  .badge.miss{background:#453112;color:var(--warn)}
  .badge.builtin{background:#14384a;color:var(--acc)}
  .detail{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:16px;display:grid;gap:14px}
  .meta{font-size:13px;color:var(--muted);display:grid;gap:3px}
  .meta code{color:var(--acc);background:#101620;padding:1px 5px;border-radius:4px}
  .slot,.mfile{background:var(--card2);border:1px solid var(--bd);border-radius:8px;padding:10px 12px;display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap}
  .sinfo{min-width:200px;flex:1}
  .sname{font-weight:600;font-size:13.5px}
  .snote{font-size:12px;color:var(--muted)}
  .sacts{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
  h3{margin:4px 0 0;font-size:14px;color:var(--acc)}
</style>
</head>
<body>
<header>
  <div>
    <h1>CZX Setup &mdash; ROM &amp; Media Uploader</h1>
    <div class="sub" id="dataDirLabel">Loading status...</div>
  </div>
  <button class="btn" onclick="loadStatus()">Refresh</button>
</header>
<main>
  <section class="dropzone" id="dropzone">
    <h2>Quick Upload: .zip Archive or Single ROM</h2>
    <p>Drop a BIOS/ROM <code>.zip</code> or ROM file here. Auto-placement matches known ROMs strictly by SHA-1 hash (filename does not matter). Use per-slot Upload below for custom/unlisted dumps.</p>
    <div class="row">
      <label style="font-size:13px;color:var(--muted)">Target scope:
        <select id="zipScope"><option value="auto">Auto-detect across all machines</option></select>
      </label>
      <input type="file" id="zipPicker" multiple style="display:none">
      <button class="btn pri" onclick="document.getElementById('zipPicker').click()">Choose .zip / ROM File(s)</button>
    </div>
  </section>

  <div class="log" id="statusLog"></div>

  <section>
    <h3 style="margin-bottom:8px">Machines</h3>
    <div class="grid" id="machineGrid"></div>
  </section>

  <section class="detail" id="machineDetail"></section>
</main>
<script>
let state = { dataDir: '', machines: [] };
let selectedId = null;

function esc(s) {
  return String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}
function fmtBytes(n) {
  if (!n) return '0 B';
  if (n < 1024) return n + ' B';
  if (n < 1048576) return (n / 1024).toFixed(1) + ' KB';
  return (n / 1048576).toFixed(2) + ' MB';
}
function showLog(msg, isErr) {
  const el = document.getElementById('statusLog');
  el.style.display = 'block';
  el.style.borderColor = isErr ? 'var(--err)' : 'var(--ok)';
  el.textContent = msg;
}
function badgeFor(m) {
  if (m.tier === 'REQUIRED_OK') return ['OK ' + m.reqFound + '/' + m.reqTotal, 'ok'];
  if (m.tier === 'REQUIRED_MISSING') return ['MISS ' + m.reqFound + '/' + m.reqTotal, 'miss'];
  if (m.tier === 'BUILTIN_PLUS_ROMS' || m.tier === 'OPTIONAL_OK') return [(m.builtinTag || 'OPT') + ' +' + m.optFound, 'ok'];
  if (m.builtinTag === 'OPT') return ['OPT', ''];
  return [m.builtinTag || 'READY', 'builtin'];
}
async function loadStatus() {
  const res = await fetch('/api/status');
  state = await res.json();
  document.getElementById('dataDirLabel').textContent = 'Data directory: ' + state.dataDir;
  if (!selectedId && state.machines.length) selectedId = state.machines[0].id;
  const scope = document.getElementById('zipScope');
  const prevScope = scope.value || 'auto';
  scope.innerHTML = '<option value="auto">Auto-detect across all machines</option>' +
    state.machines.map(m => `<option value="${esc(m.id)}">${esc(m.name)} (roms/${esc(m.romsSubDir)}/)</option>`).join('');
  scope.value = prevScope;
  renderGrid();
  renderDetail();
}
function renderGrid() {
  const g = document.getElementById('machineGrid');
  g.innerHTML = state.machines.map(m => {
    const [btxt, bcls] = badgeFor(m);
    const act = m.id === selectedId ? ' active' : '';
    return `<div class="mcard${act}" onclick="selectMachine('${esc(m.id)}')">
      <div class="mtop"><span class="mname">${esc(m.name)}</span><span class="badge ${bcls}">${esc(btxt)}</span></div>
      <div class="snote">${esc(m.subtitle)}</div>
      <div class="snote"><code>roms/${esc(m.romsSubDir)}/</code></div>
    </div>`;
  }).join('');
}
function selectMachine(id) {
  selectedId = id;
  renderGrid();
  renderDetail();
}
function renderDetail() {
  const d = document.getElementById('machineDetail');
  const m = state.machines.find(x => x.id === selectedId);
  if (!m) { d.innerHTML = ''; return; }
  const [btxt, bcls] = badgeFor(m);
  let slotsHtml = '';
  if (m.slots.length === 0) {
    slotsHtml = `<div class="snote">No external ROM files needed (${esc(m.builtinTag || 'Built-in')}).</div>`;
  } else {
    slotsHtml = m.slots.map(s => {
      const tag = s.installed
        ? (s.verified
            ? `<span class="badge ok">OK Verified (${fmtBytes(s.sizeBytes)})</span>`
            : `<span class="badge builtin">OK Custom (${fmtBytes(s.sizeBytes)})</span>`)
        : (s.required ? `<span class="badge miss">REQUIRED</span>` : `<span class="badge">OPTIONAL</span>`);
      const del = s.installed ? `<button class="btn danger" onclick="deleteRom('${esc(m.id)}','${esc(s.id)}')">Delete</button>` : '';
      const dirPrefix = s.relDir ? (s.relDir + '/') : '';
      const vnote = (s.installed && s.verifiedLabel) ? (' [' + esc(s.verifiedLabel) + ']') : '';
      return `<div class="slot">
        <div class="sinfo">
          <div class="sname">${esc(dirPrefix + s.canonical)} ${tag}</div>
          <div class="snote">${esc(s.note)}${s.installed && s.filename ? ' — installed: ' + esc(s.filename) + vnote : ''}</div>
        </div>
        <div class="sacts">
          <input type="file" id="rom_${esc(m.id)}_${esc(s.id)}" style="display:none" onchange="uploadSlot('${esc(m.id)}','${esc(s.id)}',this)">
          <button class="btn pri" onclick="document.getElementById('rom_${esc(m.id)}_${esc(s.id)}').click()">${s.installed ? 'Replace' : 'Upload ROM'}</button>
          ${del}
        </div>
      </div>`;
    }).join('');
  }
  const mediaHtml = m.media.length === 0
    ? '<div class="snote">No media files uploaded yet.</div>'
    : m.media.map(f => `<div class="mfile">
        <div class="sinfo"><span class="sname">${esc(f.name)}</span> <span class="snote">(${fmtBytes(f.sizeBytes)})</span></div>
        <button class="btn danger" onclick="deleteMedia('${esc(m.id)}','${esc(f.name)}')">Delete</button>
      </div>`).join('');

  d.innerHTML = `
    <div class="mtop">
      <div>
        <div style="font-size:18px;font-weight:700">${esc(m.name)} <span style="font-size:13px;color:var(--muted);font-weight:400">${esc(m.subtitle)}</span></div>
      </div>
      <span class="badge ${bcls}">${esc(btxt)}</span>
    </div>
    <div class="meta">
      <div>ROM directory: <code>roms/${esc(m.romsSubDir)}/</code> &mdash; Case rule: ${esc(m.caseRule)}</div>
      <div>Media directory: <code>${esc(m.mediaDir)}</code> &mdash; Extensions: ${esc(m.mediaExts)}</div>
      ${m.tip1 ? `<div>Tip: ${esc(m.tip1)}</div>` : ''}
      ${m.tip2 ? `<div style="color:var(--warn)">Note: ${esc(m.tip2)}</div>` : ''}
    </div>
    <h3>ROM Slots</h3>
    ${slotsHtml}
    <div class="mtop" style="margin-top:6px">
      <h3>Media Files (Disks / Tapes / Cartridges)</h3>
      <div>
        <input type="file" id="mediaPicker" multiple style="display:none" onchange="uploadMedia('${esc(m.id)}',this)">
        <button class="btn pri" onclick="document.getElementById('mediaPicker').click()">Upload Media</button>
      </div>
    </div>
    ${mediaHtml}`;
}
async function uploadSlot(machineId, slotId, input) {
  const file = input.files && input.files[0];
  if (!file) return;
  try {
    const res = await fetch(`/api/rom?machine=${encodeURIComponent(machineId)}&slot=${encodeURIComponent(slotId)}&filename=${encodeURIComponent(file.name)}`, {
      method: 'POST',
      body: file
    });
    const j = await res.json();
    if (!res.ok || !j.ok) throw new Error(j.error || 'Upload failed');
    let msg = 'Installed ROM: ' + j.savedPath;
    if (j.verified) msg += ' [Verified: ' + j.label + ']';
    else msg += ' (custom SHA-1: ' + (j.sha1 ? j.sha1.slice(0, 8) : '?') + '...)';
    if (j.warning) msg += '\nWarning: ' + j.warning;
    showLog(msg, false);
    await loadStatus();
  } catch (e) {
    showLog('Error: ' + e.message, true);
  } finally {
    input.value = '';
  }
}
async function deleteRom(machineId, slotId) {
  const res = await fetch(`/api/rom?machine=${encodeURIComponent(machineId)}&slot=${encodeURIComponent(slotId)}`, { method: 'DELETE' });
  const j = await res.json();
  showLog(j.ok ? 'Removed ROM slot.' : 'Failed to remove ROM.', !j.ok);
  await loadStatus();
}
async function uploadMedia(machineId, input) {
  const files = Array.from(input.files || []);
  if (!files.length) return;
  const msgs = [];
  for (const file of files) {
    const res = await fetch(`/api/media?machine=${encodeURIComponent(machineId)}&filename=${encodeURIComponent(file.name)}`, {
      method: 'POST',
      body: file
    });
    const j = await res.json();
    msgs.push(j.ok ? ('Placed media: ' + j.savedName) : ('Error (' + file.name + '): ' + (j.error || 'failed')));
  }
  showLog(msgs.join('\n'), msgs.some(m => m.startsWith('Error')));
  input.value = '';
  await loadStatus();
}
async function deleteMedia(machineId, filename) {
  const res = await fetch(`/api/media?machine=${encodeURIComponent(machineId)}&filename=${encodeURIComponent(filename)}`, { method: 'DELETE' });
  const j = await res.json();
  showLog(j.ok ? ('Removed media: ' + filename) : 'Failed to remove media.', !j.ok);
  await loadStatus();
}
async function uploadArchiveOrFiles(files) {
  const scope = document.getElementById('zipScope').value || 'auto';
  const lines = [];
  let hasErr = false;
  for (const file of files) {
    const res = await fetch(`/api/upload?machine=${encodeURIComponent(scope)}&filename=${encodeURIComponent(file.name)}`, {
      method: 'POST',
      body: file
    });
    const j = await res.json();
    if (!res.ok || !j.ok) {
      hasErr = true;
      lines.push(file.name + ': ' + (j.error || 'Upload failed'));
      continue;
    }
    if (j.placed && j.placed.length) {
      lines.push(file.name + ' -> placed ' + j.placed.length + ' file(s):\n  ' + j.placed.join('\n  '));
    } else {
      lines.push(file.name + ': 0 ROMs matched (skipped ' + j.skipped + ')');
    }
    if (j.warnings && j.warnings.length) {
      lines.push('Warnings:\n  ' + j.warnings.join('\n  '));
    }
  }
  showLog(lines.join('\n'), hasErr);
  await loadStatus();
}
document.getElementById('zipPicker').addEventListener('change', async e => {
  const files = Array.from(e.target.files || []);
  if (files.length) await uploadArchiveOrFiles(files);
  e.target.value = '';
});
const dz = document.getElementById('dropzone');
dz.addEventListener('dragover', e => { e.preventDefault(); dz.classList.add('drag'); });
dz.addEventListener('dragleave', () => dz.classList.remove('drag'));
dz.addEventListener('drop', async e => {
  e.preventDefault();
  dz.classList.remove('drag');
  const files = Array.from(e.dataTransfer.files || []);
  if (files.length) await uploadArchiveOrFiles(files);
});
loadStatus();
</script>
</body>
</html>
)CZXHTML";

}  // namespace

SetupWebServer::~SetupWebServer() {
    stop();
}

void SetupWebServer::set_last_event(std::string msg) {
    std::lock_guard<std::mutex> lock(mtx_);
    last_event_ = std::move(msg);
}

std::string SetupWebServer::last_event() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return last_event_;
}

std::vector<std::string> SetupWebServer::urls() const {
    const uint16_t p = port_.load(std::memory_order_acquire);
    if (!running_.load(std::memory_order_acquire) || p == 0) {
        return {};
    }

    std::vector<std::string> out;
    struct ifaddrs* ifa = nullptr;
    if (::getifaddrs(&ifa) == 0 && ifa != nullptr) {
        for (struct ifaddrs* cur = ifa; cur != nullptr; cur = cur->ifa_next) {
            if (!cur->ifa_addr || cur->ifa_addr->sa_family != AF_INET) continue;
            const auto* sin = reinterpret_cast<const struct sockaddr_in*>(cur->ifa_addr);
            const uint32_t addr_host = ntohl(sin->sin_addr.s_addr);
            if ((addr_host >> 24) == 127u || addr_host == 0u) continue;  // skip loopback & 0.0.0.0
            char ipbuf[INET_ADDRSTRLEN]{};
            if (::inet_ntop(AF_INET, &sin->sin_addr, ipbuf, sizeof(ipbuf))) {
                std::string u = std::string("http://") + ipbuf + ":" + std::to_string(p) + "/";
                if (std::find(out.begin(), out.end(), u) == out.end()) {
                    out.push_back(std::move(u));
                }
            }
        }
        ::freeifaddrs(ifa);
    }

    if (out.empty()) {
        out.push_back(std::string("http://127.0.0.1:") + std::to_string(p) + "/");
    }
    return out;
}

bool SetupWebServer::start(const std::string& data_dir, uint16_t preferred_port) {
    if (running_.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(mtx_);
        data_dir_ = data_dir;
        return true;
    }

    stop();  // ensure clean state

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        set_last_event(std::string("Socket error: ") + std::strerror(errno));
        return false;
    }
    set_cloexec(fd);

    const int yes = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    auto try_bind = [fd](uint16_t port_num) -> bool {
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port_num);
        return ::bind(fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)) == 0;
    };

    if (!try_bind(preferred_port)) {
        if (preferred_port == 0 || !try_bind(0)) {
            set_last_event(std::string("Bind error: ") + std::strerror(errno));
            ::close(fd);
            return false;
        }
    }

    if (::listen(fd, 8) != 0) {
        set_last_event(std::string("Listen error: ") + std::strerror(errno));
        ::close(fd);
        return false;
    }

    struct sockaddr_in bound{};
    socklen_t blen = sizeof(bound);
    if (::getsockname(fd, reinterpret_cast<struct sockaddr*>(&bound), &blen) != 0) {
        ::close(fd);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        data_dir_ = data_dir;
        last_event_ = "Server ready (waiting for uploads)";
    }
    listen_fd_ = fd;
    port_.store(ntohs(bound.sin_port), std::memory_order_release);
    stop_flag_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    worker_ = std::thread(&SetupWebServer::serve_loop, this);
    return true;
}

void SetupWebServer::stop() {
    stop_flag_.store(true, std::memory_order_release);
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    running_.store(false, std::memory_order_release);
    port_.store(0, std::memory_order_release);
}

void SetupWebServer::serve_loop() {
    while (!stop_flag_.load(std::memory_order_acquire)) {
        struct pollfd pfd{};
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;
        const int pr = ::poll(&pfd, 1, 200);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0 || (pfd.revents & POLLIN) == 0) {
            continue;
        }
        struct sockaddr_in peer{};
        socklen_t plen = sizeof(peer);
        const int cfd = ::accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&peer), &plen);
        if (cfd < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            break;
        }
        set_cloexec(cfd);
        handle_client(cfd);
        ::close(cfd);
    }
}

void SetupWebServer::handle_client(int client_fd) {
    struct timeval tv{};
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    (void)::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)::setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::string raw;
    raw.reserve(4096);
    size_t hdr_end = std::string::npos;
    char buf[4096];
    while ((hdr_end = raw.find("\r\n\r\n")) == std::string::npos) {
        if (raw.size() >= kMaxHeaderBytes) {
            send_http_response(client_fd, 431, "Request Header Fields Too Large", "text/plain", "Headers too large");
            return;
        }
        const ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
        if (n <= 0) return;
        raw.append(buf, static_cast<size_t>(n));
    }

    const std::string hdr_block = raw.substr(0, hdr_end);
    const size_t body_offset = hdr_end + 4;

    const size_t line_end = hdr_block.find("\r\n");
    const std::string req_line = (line_end == std::string::npos) ? hdr_block : hdr_block.substr(0, line_end);

    std::istringstream rl(req_line);
    std::string method;
    std::string target;
    rl >> method >> target;
    if (method.empty() || target.empty()) {
        send_http_response(client_fd, 400, "Bad Request", "text/plain", "Malformed request");
        return;
    }

    size_t content_length = 0;
    std::string content_type;
    std::string x_filename;
    if (line_end != std::string::npos) {
        size_t pos = line_end + 2;
        while (pos < hdr_block.size()) {
            const size_t next = hdr_block.find("\r\n", pos);
            const std::string hline = (next == std::string::npos) ? hdr_block.substr(pos) : hdr_block.substr(pos, next - pos);
            const size_t colon = hline.find(':');
            if (colon != std::string::npos) {
                const std::string key = to_lower_ascii(trim_ascii(hline.substr(0, colon)));
                const std::string val = trim_ascii(hline.substr(colon + 1));
                if (key == "content-length") {
                    content_length = static_cast<size_t>(std::strtoull(val.c_str(), nullptr, 10));
                } else if (key == "content-type") {
                    content_type = val;
                } else if (key == "x-filename") {
                    x_filename = url_decode(val);
                }
            }
            if (next == std::string::npos) break;
            pos = next + 2;
        }
    }

    if (content_length > kMaxBodyBytes) {
        send_json(client_fd, 413, "Payload Too Large", "{\"ok\":false,\"error\":\"Upload exceeds 64MB limit\"}");
        return;
    }

    std::vector<uint8_t> body;
    if (content_length > 0) {
        body.reserve(content_length);
        const size_t already = raw.size() - body_offset;
        const size_t copy_now = std::min(already, content_length);
        if (copy_now > 0) {
            body.insert(body.end(),
                        reinterpret_cast<const uint8_t*>(raw.data() + body_offset),
                        reinterpret_cast<const uint8_t*>(raw.data() + body_offset + copy_now));
        }
        while (body.size() < content_length) {
            const size_t want = std::min(sizeof(buf), content_length - body.size());
            const ssize_t n = ::recv(client_fd, buf, want, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                send_json(client_fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"Incomplete request body\"}");
                return;
            }
            if (n == 0) {
                send_json(client_fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"Connection closed before body end\"}");
                return;
            }
            body.insert(body.end(),
                        reinterpret_cast<const uint8_t*>(buf),
                        reinterpret_cast<const uint8_t*>(buf + n));
        }
    }

    const size_t qpos = target.find('?');
    const std::string path = (qpos == std::string::npos) ? target : target.substr(0, qpos);
    const auto query = (qpos == std::string::npos)
                             ? std::unordered_map<std::string, std::string>{}
                             : parse_query(target.substr(qpos + 1));

    auto get_param = [&query](const char* key) -> std::string {
        const auto it = query.find(key);
        return (it != query.end()) ? it->second : std::string{};
    };

    std::string data_dir_copy;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        data_dir_copy = data_dir_;
    }

    if (method == "GET" && (path == "/" || path == "/index.html")) {
        send_http_response(client_fd, 200, "OK", "text/html; charset=utf-8", kWebUiHtml);
        return;
    }

    if (method == "GET" && path == "/api/status") {
        send_json(client_fd, 200, "OK", build_status_json(data_dir_copy));
        return;
    }

    // Extract multipart file payload if client sent multipart/form-data.
    std::string upload_filename = !x_filename.empty() ? x_filename : get_param("filename");
    if (to_lower_ascii(content_type).find("multipart/form-data") != std::string::npos) {
        std::string mp_name;
        std::vector<uint8_t> mp_bytes;
        if (extract_multipart_file(content_type, body, mp_name, mp_bytes)) {
            if (!mp_name.empty()) upload_filename = mp_name;
            body = std::move(mp_bytes);
        }
    }

    if (method == "POST" && path == "/api/rom") {
        const std::string machine_id = get_param("machine");
        const std::string slot_id = get_param("slot");
        std::string saved_rel;
        std::string err;
        if (!install_single_rom(data_dir_copy, machine_id, slot_id, upload_filename,
                                body.data(), body.size(), saved_rel, err)) {
            send_json(client_fd, 400, "Bad Request",
                      "{\"ok\":false,\"error\":\"" + json_escape(err) + "\"}");
            return;
        }
        const KnownRomEntry* hit = identify_rom_by_hash(body.data(), body.size());
        const std::string digest = sha1_hex(body.data(), body.size());
        const bool same_slot = (hit != nullptr && machine_id == hit->machine_id && slot_id == hit->slot_id);
        const std::string label = hit ? std::string(hit->label) : std::string("Custom / unlisted ROM");
        std::string warning;
        if (hit != nullptr && !same_slot) {
            warning = std::string("Uploaded bytes match ") + hit->machine_id + "/" + hit->slot_id +
                      " [" + hit->label + "] (saved manually here anyway)";
        }
        upload_count_.fetch_add(1, std::memory_order_relaxed);
        set_last_event("Placed " + saved_rel);
        send_json(client_fd, 200, "OK",
                  std::string("{\"ok\":true,\"savedPath\":\"") + json_escape(saved_rel) +
                      "\",\"sha1\":\"" + digest +
                      "\",\"verified\":" + (same_slot ? "true" : "false") +
                      ",\"label\":\"" + json_escape(label) +
                      "\",\"warning\":\"" + json_escape(warning) + "\"}");
        return;
    }

    if (method == "DELETE" && path == "/api/rom") {
        const std::string machine_id = get_param("machine");
        const std::string slot_id = get_param("slot");
        const bool ok = delete_slot_rom(data_dir_copy, machine_id, slot_id);
        if (ok) {
            set_last_event("Removed " + machine_id + "/" + slot_id);
        }
        send_json(client_fd, ok ? 200 : 404, ok ? "OK" : "Not Found",
                  std::string("{\"ok\":") + (ok ? "true" : "false") + "}");
        return;
    }

    if (method == "POST" && path == "/api/upload") {
        std::string scope = get_param("machine");
        if (scope.empty()) scope = "auto";
        if (upload_filename.empty()) upload_filename = "upload.bin";
        const ImportReport rep = import_rom_upload(data_dir_copy, scope, upload_filename, body.data(), body.size());
        if (!rep.placed.empty()) {
            upload_count_.fetch_add(static_cast<int>(rep.placed.size()), std::memory_order_relaxed);
            set_last_event("Imported " + std::to_string(rep.placed.size()) + " ROM(s): " + rep.placed.front());
        } else {
            set_last_event("Upload skipped " + std::to_string(rep.skipped) + " file(s)");
        }
        std::ostringstream js;
        js << "{\"ok\":true,\"placed\":[";
        for (size_t i = 0; i < rep.placed.size(); ++i) {
            if (i > 0) js << ',';
            js << '"' << json_escape(rep.placed[i]) << '"';
        }
        js << "],\"warnings\":[";
        for (size_t i = 0; i < rep.warnings.size(); ++i) {
            if (i > 0) js << ',';
            js << '"' << json_escape(rep.warnings[i]) << '"';
        }
        js << "],\"skipped\":" << rep.skipped << '}';
        send_json(client_fd, 200, "OK", js.str());
        return;
    }

    if (method == "POST" && path == "/api/media") {
        const std::string machine_id = get_param("machine");
        std::string saved_name;
        std::string err;
        if (!install_media_file(data_dir_copy, machine_id, upload_filename,
                                body.data(), body.size(), saved_name, err)) {
            send_json(client_fd, 400, "Bad Request",
                      "{\"ok\":false,\"error\":\"" + json_escape(err) + "\"}");
            return;
        }
        upload_count_.fetch_add(1, std::memory_order_relaxed);
        set_last_event("Media " + machine_id + "/" + saved_name);
        send_json(client_fd, 200, "OK",
                  "{\"ok\":true,\"savedName\":\"" + json_escape(saved_name) + "\"}");
        return;
    }

    if (method == "DELETE" && path == "/api/media") {
        const std::string machine_id = get_param("machine");
        const std::string filename = get_param("filename");
        const bool ok = delete_media_file(data_dir_copy, machine_id, filename);
        if (ok) {
            set_last_event("Deleted media " + filename);
        }
        send_json(client_fd, ok ? 200 : 404, ok ? "OK" : "Not Found",
                  std::string("{\"ok\":") + (ok ? "true" : "false") + "}");
        return;
    }

    send_http_response(client_fd, 404, "Not Found", "text/plain", "Not Found");
}

}  // namespace czx::core
