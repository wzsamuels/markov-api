#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>
#include <sqlite3.h>

namespace fs = std::filesystem;

// Helper to execute simple SQL commands
void execute_sql(sqlite3* db, const std::string& sql) {
    char* errMsg = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "SQL Error: " << errMsg << "\n";
        sqlite3_free(errMsg);
    }
}

// Basic tokenization by whitespace
std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) {
        tokens.push_back(word);
    }
    return tokens;
}

std::string clean_gutenberg_text(const std::string& text) {
    // 1. Locate the START marker
    size_t start_pos = text.find("*** START OF THE PROJECT GUTENBERG");
    if (start_pos == std::string::npos) {
        start_pos = text.find("*** START OF THIS PROJECT GUTENBERG");
    }
    
    // If we found the start text, we need to advance past its closing asterisks
    if (start_pos != std::string::npos) {
        size_t closing_asterisks = text.find("***", start_pos + 10);
        if (closing_asterisks != std::string::npos) {
            start_pos = closing_asterisks + 3; // Move the index completely past the asterisks
        } else {
            start_pos = 0; // Fallback if formatting is completely broken
        }
    } else {
        start_pos = 0;
    }

    // 2. Locate the END marker (starting the search from where the header ended)
    size_t end_pos = text.find("*** END OF THE PROJECT GUTENBERG", start_pos);
    if (end_pos == std::string::npos) {
        end_pos = text.find("*** END OF THIS PROJECT GUTENBERG", start_pos);
    }

    // If no end marker is found, we'll just read to the end of the file
    if (end_pos == std::string::npos) {
        end_pos = text.length();
    }

    // 3. Slice and return the actual book text
    if (start_pos < end_pos) {
        return text.substr(start_pos, end_pos - start_pos);
    }
    
    return text;
}

int main() {
    sqlite3* db;
    if (sqlite3_open("markov-soda.db", &db) != SQLITE_OK) {
        std::cerr << "Failed to open database\n";
        return 1;
    }

    // Optimize SQLite for massive bulk inserts
    execute_sql(db, "PRAGMA journal_mode = WAL;");
    execute_sql(db, "PRAGMA synchronous = NORMAL;");

    // Initialize Schema
    execute_sql(db, R"(
        CREATE TABLE IF NOT EXISTS n_grams (
            state TEXT PRIMARY KEY,
            is_start BOOLEAN DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS transitions (
            state TEXT,
            next_token TEXT,
            frequency INTEGER DEFAULT 1,
            PRIMARY KEY (state, next_token),
            FOREIGN KEY (state) REFERENCES n_grams(state)
        );
        CREATE TABLE IF NOT EXISTS reverse_transitions (
            state TEXT,
            prev_token TEXT,
            frequency INTEGER DEFAULT 1,
            PRIMARY KEY (state, prev_token),
            FOREIGN KEY (state) REFERENCES n_grams(state)
        );
        CREATE INDEX IF NOT EXISTS idx_transitions_state ON transitions(state);
        CREATE INDEX IF NOT EXISTS idx_reverse_state ON reverse_transitions(state);

        CREATE INDEX IF NOT EXISTS idx_ngrams_nocase ON n_grams(state COLLATE NOCASE);
    )");

    // Prepare SQL statements once to maximize loop efficiency
    sqlite3_stmt* insert_state;
    sqlite3_stmt* insert_transition;
    sqlite3_stmt* insert_reverse;
    
    const char* sql_state = "INSERT INTO n_grams (state, is_start) VALUES (?, ?) ON CONFLICT(state) DO UPDATE SET is_start = excluded.is_start OR is_start;";
    const char* sql_trans = "INSERT INTO transitions (state, next_token, frequency) VALUES (?, ?, 1) ON CONFLICT(state, next_token) DO UPDATE SET frequency = frequency + 1;";
    const char* sql_rev = "INSERT INTO reverse_transitions (state, prev_token, frequency) VALUES (?, ?, 1) ON CONFLICT(state, prev_token) DO UPDATE SET frequency = frequency + 1;";
    
    sqlite3_prepare_v2(db, sql_state, -1, &insert_state, nullptr);
    sqlite3_prepare_v2(db, sql_trans, -1, &insert_transition, nullptr);
    sqlite3_prepare_v2(db, sql_rev, -1, &insert_reverse, nullptr);

    // Wrap the entire parsing phase in a single transaction
    execute_sql(db, "BEGIN TRANSACTION;");

    int order = 3; // Match your bot's Markov order
    std::string data_path = "./data/soda_data"; // Path to your Gutenberg text files

    int counter = 0;
    for (const auto& entry : fs::directory_iterator(data_path)) {
        counter++;
        if (counter == 600) break; // Limit to 300 files for testing

        if (entry.path().extension() == ".txt") {
            std::cout << "Processing: " << entry.path().filename() << "\n";
            
            std::ifstream file(entry.path());
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            
            content = clean_gutenberg_text(content);            
            
            std::vector<std::string> tokens = tokenize(content);
            if (tokens.size() <= order) continue;

            for (size_t i = 0; i < tokens.size() - order; ++i) {
                std::string state = "";
                for (int j = 0; j < order; ++j) {
                    state += tokens[i + j] + (j < order - 1 ? " " : "");
                }
                std::string next_token = tokens[i + order];

                // Determine if this is a starting state
                int is_start = 0;
                if (i == 0) {
                    is_start = 1;
                } else {
                    char last_char = tokens[i - 1].back();
                    if (last_char == '.' || last_char == '?' || last_char == '!') is_start = 1;
                }            

                // Bind and execute State insertion
                sqlite3_bind_text(insert_state, 1, state.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(insert_state, 2, is_start);
                sqlite3_step(insert_state);
                sqlite3_reset(insert_state);

                // Bind and execute Transition insertion
                sqlite3_bind_text(insert_transition, 1, state.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(insert_transition, 2, next_token.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(insert_transition);
                sqlite3_reset(insert_transition);

                // Bind and execute Reverse Transition insertion
                if (i > 0) {
                    std::string prev_token = tokens[i - 1];
                    sqlite3_bind_text(insert_reverse, 1, state.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(insert_reverse, 2, prev_token.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(insert_reverse);
                    sqlite3_reset(insert_reverse);
                }
            }
        }
    }

    // Commit the transaction to disk
    execute_sql(db, "COMMIT;");

    // Clean up
    sqlite3_finalize(insert_state);
    sqlite3_finalize(insert_transition);
    sqlite3_finalize(insert_reverse);
    sqlite3_close(db);

    std::cout << "Database build complete.\n";
    return 0;
}