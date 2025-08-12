#include <string>

namespace Blog{
inline std::string html_escape(const std::string& input) {
  std::string output;
  output.reserve(input.size() * 1.2);

  for (char c : input) {
      switch (c) {
          case '<':  output.append("&lt;");   break;
          case '>':  output.append("&gt;");   break;
          case '&':  output.append("&amp;");  break;
          case '"':  output.append("&quot;"); break;
          case '\'': output.append("&apos;"); break;
          default:   output.push_back(c);     break;
      }
  }

  return output;
}

inline void append_codepoint_utf8(std::string &out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        // replacement char U+FFFD
        out.push_back(static_cast<char>(0xEF));
        out.push_back(static_cast<char>(0xBF));
        out.push_back(static_cast<char>(0xBD));
    }
}

inline std::string html_unescape(const std::string &input) {
    std::string out;
    out.reserve(input.size());

    const size_t n = input.size();
    for (size_t i = 0; i < n; ++i) {
        char c = input[i];
        if (c != '&') {
            out.push_back(c);
            continue;
        }

        // find terminating ';'
        size_t semi = input.find(';', i + 1);
        if (semi == std::string::npos) {
            // no semicolon -> treat '&' as literal
            out.push_back('&');
            continue;
        }

        // entity body between '&' and ';'
        size_t start = i + 1;
        if (start >= semi) { // weird "&;"
            out.append("&;");
            i = semi;
            continue;
        }
        std::string ent = input.substr(start, semi - start);

        // named entities we support
        if (ent == "lt") {
            out.push_back('<'); i = semi; continue;
        } else if (ent == "gt") {
            out.push_back('>'); i = semi; continue;
        } else if (ent == "amp") {
            out.push_back('&'); i = semi; continue;
        } else if (ent == "quot") {
            out.push_back('"'); i = semi; continue;
        } else if (ent == "apos") {
            out.push_back('\''); i = semi; continue;
        }

        // numeric entity: starts with '#'
        if (!ent.empty() && ent[0] == '#') {
            uint32_t codepoint = 0;
            bool ok = false;
            if (ent.size() >= 2 && (ent[1] == 'x' || ent[1] == 'X')) {
                // hex
                size_t pos = 2;
                if (pos < ent.size()) {
                    ok = true;
                    for (; pos < ent.size(); ++pos) {
                        char ch = ent[pos];
                        codepoint <<= 4;
                        if (ch >= '0' && ch <= '9') codepoint += ch - '0';
                        else if (ch >= 'a' && ch <= 'f') codepoint += 10 + (ch - 'a');
                        else if (ch >= 'A' && ch <= 'F') codepoint += 10 + (ch - 'A');
                        else { ok = false; break; }
                    }
                }
            } else {
                // decimal
                size_t pos = 1;
                if (pos < ent.size()) {
                    ok = true;
                    for (; pos < ent.size(); ++pos) {
                        char ch = ent[pos];
                        if (ch >= '0' && ch <= '9') {
                            codepoint = codepoint * 10 + (ch - '0');
                        } else { ok = false; break; }
                    }
                }
            }

            if (ok) {
                // basic validation: map surrogate halves and too-large codepoints to replacement char
                if ((codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF) {
                    // U+FFFD
                    append_codepoint_utf8(out, 0xFFFD);
                } else {
                    append_codepoint_utf8(out, codepoint);
                }
                i = semi;
                continue;
            }
        }

        // unknown entity -> copy as-is (keep it safe and predictable)
        out.append("&");
        out.append(ent);
        out.push_back(';');
        i = semi;
    }

    return out;
}

}