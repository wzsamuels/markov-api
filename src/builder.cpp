#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <map>
#include <filesystem>
#include <string_view>
#include <sqlite3.h>

namespace fs = std::filesystem;

// Configuration
const int MARKOV_ORDER = 3;
const int MIN_FREQUENCY = 2; // Strategy 2: Prune transitions occurring fewer than 2 times
//const std::string DATA_PATH = "./data/soda_data";
//const std::string DB_PATH = "markov-soda-optimized.db";

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

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv, argv + argc);

    for (const auto& arg : args) {
        std::cout << arg << "\n";
    }

    if (argc != 3) {
        std::cout << "Provide input directory and output file as arguments.\n";

        return 1; 
    }

    const std::string DATA_PATH = args[1];
    const std::string DB_PATH = args[2];

    // --- Strategy 1 & 2: In-Memory Data Structures ---
    std::unordered_map<std::string, int> vocab_to_id;
    std::vector<std::string> id_to_vocab;

    // Helper lambda for integer token mapping
    auto get_or_create_token_id = [&](const std::string& token) -> int {
        auto it = vocab_to_id.find(token);
        if (it != vocab_to_id.end()) {
            return it->second;
        }
        int new_id = static_cast<int>(id_to_vocab.size());
        vocab_to_id[token] = new_id;
        id_to_vocab.push_back(token);
        return new_id;
    };

    // State representation using an array of 3 integer token IDs
    using StateVec = std::vector<int>; 
    std::map<StateVec, int> state_to_id;
    std::vector<StateVec> id_to_state;
    std::unordered_map<int, bool> state_is_start;

    auto get_or_create_state_id = [&](const StateVec& state_vec, bool is_start) -> int {
        auto it = state_to_id.find(state_vec);
        int state_id;
        if (it != state_to_id.end()) {
            state_id = it->second;
            if (is_start) state_is_start[state_id] = true;
        } else {
            state_id = static_cast<int>(id_to_state.size());
            state_to_id[state_vec] = state_id;
            id_to_state.push_back(state_vec);
            state_is_start[state_id] = is_start;
        }
        return state_id;
    };

    // Transition maps: <pair<state_id, token_id>, count>
    std::map<std::pair<int, int>, int> forward_counts;
    std::map<std::pair<int, int>, int> reverse_counts;

    std::cout << "Parsing text files into memory...\n";

    int counter = 0;
    for (const auto& entry : fs::directory_iterator(DATA_PATH)) {
        if (entry.path().extension() == ".txt") {
            counter++;
            if (counter % 100 == 0) {
                std::cout << "Processed " << counter << " files...\n";
            }

            std::ifstream file(entry.path());
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();

            std::vector<std::string> raw_tokens = tokenize(content);
            if (raw_tokens.size() <= MARKOV_ORDER) continue;

            // Convert string tokens to integer IDs
            std::vector<int> token_ids;
            token_ids.reserve(raw_tokens.size());
            for (const auto& token : raw_tokens) {
                token_ids.push_back(get_or_create_token_id(token));
            }

            // Build n-grams and transition frequencies in RAM
            for (size_t i = 0; i <= token_ids.size() - MARKOV_ORDER - 1; ++i) {
                StateVec state_vec(token_ids.begin() + i, token_ids.begin() + i + MARKOV_ORDER);

                bool is_start = false;
                if (i == 0) {
                    is_start = true;
                } else {
                    char last_char = raw_tokens[i - 1].back();
                    if (last_char == '.' || last_char == '?' || last_char == '!') {
                        is_start = true;
                    }
                }

                int state_id = get_or_create_state_id(state_vec, is_start);
                int next_token_id = token_ids[i + MARKOV_ORDER];
                forward_counts[{state_id, next_token_id}]++;

                if (i > 0) {
                    int prev_token_id = token_ids[i - 1];
                    reverse_counts[{state_id, prev_token_id}]++;
                }
            }
        }
    }

    std::cout << "Parsing complete.\n";
    std::cout << "Unique Tokens: " << id_to_vocab.size() << "\n";
    std::cout << "Unique States: " << id_to_state.size() << "\n";

    // --- Strategy 3: Database Schema Initialization ---
    sqlite3* db;
    if (sqlite3_open(DB_PATH.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Failed to open database\n";
        return 1;
    }

    execute_sql(db, "PRAGMA journal_mode = WAL;");
    execute_sql(db, "PRAGMA synchronous = NORMAL;");

    // Optimized Schema using INTEGER keys and WITHOUT ROWID
    execute_sql(db, R"(
        CREATE TABLE IF NOT EXISTS vocabulary (
            id INTEGER PRIMARY KEY,
            token TEXT UNIQUE
        );

        CREATE TABLE IF NOT EXISTS n_grams (
            id INTEGER PRIMARY KEY,
            token1_id INT,
            token2_id INT,
            token3_id INT,
            is_start INT
        );

        CREATE TABLE IF NOT EXISTS transitions (
            state_id INT,
            next_token_id INT,
            frequency INT,
            PRIMARY KEY (state_id, next_token_id)
        ) WITHOUT ROWID;

        CREATE TABLE IF NOT EXISTS reverse_transitions (
            state_id INT,
            prev_token_id INT,
            frequency INT,
            PRIMARY KEY (state_id, prev_token_id)
        ) WITHOUT ROWID;

        CREATE INDEX IF NOT EXISTS idx_transitions_state ON transitions(state_id);
        CREATE INDEX IF NOT EXISTS idx_reverse_state ON reverse_transitions(state_id);
    )");

    std::cout << "Writing optimized data to SQLite...\n";
    execute_sql(db, "BEGIN TRANSACTION;");

    // 1. Flush Vocabulary
    sqlite3_stmt* stmt_vocab;
    sqlite3_prepare_v2(db, "INSERT INTO vocabulary (id, token) VALUES (?, ?);", -1, &stmt_vocab, nullptr);
    for (size_t id = 0; id < id_to_vocab.size(); ++id) {
        sqlite3_bind_int(stmt_vocab, 1, static_cast<int>(id));
        sqlite3_bind_text(stmt_vocab, 2, id_to_vocab[id].c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt_vocab);
        sqlite3_reset(stmt_vocab);
    }
    sqlite3_finalize(stmt_vocab);

    // 2. Flush States
    sqlite3_stmt* stmt_state;
    sqlite3_prepare_v2(db, "INSERT INTO n_grams (id, token1_id, token2_id, token3_id, is_start) VALUES (?, ?, ?, ?, ?);", -1, &stmt_state, nullptr);
    for (size_t id = 0; id < id_to_state.size(); ++id) {
        sqlite3_bind_int(stmt_state, 1, static_cast<int>(id));
        sqlite3_bind_int(stmt_state, 2, id_to_state[id][0]);
        sqlite3_bind_int(stmt_state, 3, id_to_state[id][1]);
        sqlite3_bind_int(stmt_state, 4, id_to_state[id][2]);
        sqlite3_bind_int(stmt_state, 5, state_is_start[static_cast<int>(id)] ? 1 : 0);
        sqlite3_step(stmt_state);
        sqlite3_reset(stmt_state);
    }
    sqlite3_finalize(stmt_state);

    // 3. Flush Forward Transitions (Applying Frequency Pruning)
    sqlite3_stmt* stmt_trans;
    sqlite3_prepare_v2(db, "INSERT INTO transitions (state_id, next_token_id, frequency) VALUES (?, ?, ?);", -1, &stmt_trans, nullptr);
    for (const auto& [key, freq] : forward_counts) {
        if (freq < MIN_FREQUENCY) continue; // PRUNING

        sqlite3_bind_int(stmt_trans, 1, key.first);
        sqlite3_bind_int(stmt_trans, 2, key.second);
        sqlite3_bind_int(stmt_trans, 3, freq);
        sqlite3_step(stmt_trans);
        sqlite3_reset(stmt_trans);
    }
    sqlite3_finalize(stmt_trans);

    // 4. Flush Reverse Transitions (Applying Frequency Pruning)
    sqlite3_stmt* stmt_rev;
    sqlite3_prepare_v2(db, "INSERT INTO reverse_transitions (state_id, prev_token_id, frequency) VALUES (?, ?, ?);", -1, &stmt_rev, nullptr);
    for (const auto& [key, freq] : reverse_counts) {
        if (freq < MIN_FREQUENCY) continue; // PRUNING

        sqlite3_bind_int(stmt_rev, 1, key.first);
        sqlite3_bind_int(stmt_rev, 2, key.second);
        sqlite3_bind_int(stmt_rev, 3, freq);
        sqlite3_step(stmt_rev);
        sqlite3_reset(stmt_rev);
    }
    sqlite3_finalize(stmt_rev);

    execute_sql(db, "COMMIT;");
    sqlite3_close(db);

    std::cout << "Database build complete. Saved to " << DB_PATH << "\n";
    return 0;
}