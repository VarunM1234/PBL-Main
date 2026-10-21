/*
 * PURPOSE: Hand-written CSV reader: quoted fields, '#' comment lines, errors with file name + line number.
 * WHY THIS NAME: reads comma-separated values.
 * CONNECTS TO: catalog (loads every file in data/).
 * COMPLEXITY: O(file size).
 */
#ifndef SMARTROUTE_CSV_HPP
#define SMARTROUTE_CSV_HPP
#include <string>
#include <vector>

namespace sr {

struct CsvRow { int line; std::vector<std::string> fields; };

/* Reads the file; the first non-comment line is the header and is skipped.
 * Every data row must have exactly expected_columns fields. Returns false and sets error on failure. */
bool csv_read(const std::string &path, size_t expected_columns, std::vector<CsvRow> *rows, std::string *error);
std::vector<std::string> split_fields(const std::string &text, char sep);   /* plain split, no quotes */

}  // namespace sr
#endif
