#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <iostream>
#include <vector>
#include <mutex>

struct AnalysisData {
    float bpm;
    float offset;
};

class AnalysisDB {
public:
    AnalysisDB(const std::string& filename = "analysis.db") : dbFile(filename) {
        load();
    }

    void load() {
        std::lock_guard<std::mutex> lock(dbMutex);
        std::ifstream file(dbFile);
        if (!file.is_open()) return;
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            uint64_t hash;
            float bpm, offset;
            if (ss >> hash >> bpm >> offset) {
                cache[hash] = {bpm, offset};
            }
        }
    }

    void save(uint64_t hash, float bpm, float offset) {
        std::lock_guard<std::mutex> lock(dbMutex);
        cache[hash] = {bpm, offset};
        
        std::ofstream file(dbFile); // Truncate and rewrite
        for (const auto& kv : cache) {
            file << kv.first << " " << kv.second.bpm << " " << kv.second.offset << "\n";
        }
    }
    
bool get(uint64_t hash, float& bpm, float& offset) {
        std::lock_guard<std::mutex> lock(dbMutex);
        auto it = cache.find(hash);
        if (it != cache.end()) {
            bpm = it->second.bpm;
            offset = it->second.offset;
            return true;
        }
        return false;
    }

    static uint64_t computeHash(const std::string& filepath) {
        // FNV-1a 64-bit
        uint64_t hash = 14695981039346656037ULL;
        const uint64_t prime = 1099511628211ULL;
        
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) return 0;

        char buffer[16384]; // 16KB chunks
        while (file.read(buffer, sizeof(buffer))) {
            for (std::streamsize i = 0; i < file.gcount(); ++i) {
                hash ^= (uint8_t)buffer[i];
                hash *= prime;
            }
        }
        // Handle remaining
        for (std::streamsize i = 0; i < file.gcount(); ++i) {
            hash ^= (uint8_t)buffer[i];
            hash *= prime;
        }
        
        return hash;
    }

private:
    std::string dbFile;
    std::unordered_map<uint64_t, AnalysisData> cache;
    std::mutex dbMutex;
};
