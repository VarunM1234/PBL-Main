/*
 * PURPOSE: CSV reader implementation.
 * WHY THIS NAME: implements csv.hpp.
 * CONNECTS TO: csv.hpp
 * COMPLEXITY: O(file size).
 */
#include "csv.hpp"
#include <fstream>

namespace sr {

std::vector<std::string> split_fields(const std::string &text, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

/* Splits one CSV line honouring "double quotes" (a doubled "" inside quotes is one quote). */
static bool parse_line(const std::string &line, std::vector<std::string> *fields) {
    std::string cur;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; i++; }
            else if (c == '"') quoted = false;
            else cur += c;
        } else if (c == '"') quoted = true;
        else if (c == ',') { fields->push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    fields->push_back(cur);
    return !quoted;                                  /* false = unterminated quote */
}

bool csv_read(const std::string &path, size_t columns, std::vector<CsvRow> *rows, std::string *error) {
    std::ifstream in(path);
    if (!in) { *error = path + ": cannot open file"; return false; }
    std::string line;
    int line_no = 0;
    bool header_seen = false;
    while (std::getline(in, line)) {
        line_no++;
        if (line.empty() || line[0] == '#' || line == "\r") continue;
        CsvRow row{line_no, {}};
        if (!parse_line(line, &row.fields)) { *error = path + ":" + std::to_string(line_no) + ": unterminated quote"; return false; }
        if (row.fields.size() != columns) {
            *error = path + ":" + std::to_string(line_no) + ": expected " + std::to_string(columns) +
                     " columns but found " + std::to_string(row.fields.size());
            return false;
        }
        if (!header_seen) { header_seen = true; continue; }
        rows->push_back(row);
    }
    return true;
}

}  // namespace sr
