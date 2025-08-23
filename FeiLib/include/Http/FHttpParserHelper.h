#ifndef FHTTP_PARSER_HELPER_H
#define FHTTP_PARSER_HELPER_H
#include <string>
#include "Http/FHttpDef.h"
#include "sstream"

namespace Fei::Http {

	class ParserUtils {
	public:
        inline static std::string UrlDecode(const std::string& s) {
            std::string out;
            out.reserve(s.size());
            for (size_t i = 0; i < s.size(); ++i) {
                char c = s[i];
                if (c == '%') {
                    if (i + 2 < s.size()) {
                        int hi = std::isdigit(s[i + 1]) ? s[i + 1] - '0'
                            : std::toupper(s[i + 1]) - 'A' + 10;
                        int lo = std::isdigit(s[i + 2]) ? s[i + 2] - '0'
                            : std::toupper(s[i + 2]) - 'A' + 10;
                        out.push_back(static_cast<char>((hi << 4) | lo));
                        i += 2;
                    }
                }
                else if (c == '+') {
                    out.push_back(' ');
                }
                else {
                    out.push_back(c);
                }
            }
            return out;
        }

        inline static void ParsePathLine(const std::string& line,std::string& outPath,HttpQueryMap& outMap) {
            auto qm = line.find('?');
            if (qm == std::string::npos) {
                return; // No query
            }

            std::string query = line.substr(qm + 1);
            outPath = line.substr(0, qm);
            // Remove fragment identifier
            auto hash = query.find('#');
            if (hash != std::string::npos) {
                query.resize(hash);
            }

            std::stringstream ss(query);
            std::string pair;
            while (std::getline(ss, pair, '&')) {
                if (pair.empty())
                    continue;

                auto eq = pair.find('=');
                std::string key, value;
                if (eq != std::string::npos) {
                    key = UrlDecode(pair.substr(0, eq));
                    value = UrlDecode(pair.substr(eq + 1));
                }
                else {
                    key = UrlDecode(pair);
                    value = "";
                }
                outMap.insert({ std::move(key), std::move(value) });
            }
        }
	};

};

#endif