#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <iostream>
#include <vector>
#include <mutex>
#include <filesystem>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cctype>

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
            std::string hash;
            float bpm, offset;
            if (ss >> hash >> bpm >> offset) {
                cache[hash] = {bpm, offset};
            }
        }
    }

    void save(const std::string& hash, float bpm, float offset) {
        std::lock_guard<std::mutex> lock(dbMutex);
        cache[hash] = {bpm, offset};
        
        std::ofstream file(dbFile); // Truncate and rewrite
        for (const auto& kv : cache) {
            file << kv.first << " " << kv.second.bpm << " " << kv.second.offset << "\n";
        }
    }
    
    // Removes a track's cached analysis (if present) and rewrites the file.
    void remove(const std::string& hash) {
        std::lock_guard<std::mutex> lock(dbMutex);
        if (cache.erase(hash) == 0) return;
        std::ofstream file(dbFile); // Truncate and rewrite
        for (const auto& kv : cache) {
            file << kv.first << " " << kv.second.bpm << " " << kv.second.offset << "\n";
        }
    }

    bool get(const std::string& hash, float& bpm, float& offset) {
        std::lock_guard<std::mutex> lock(dbMutex);
        auto it = cache.find(hash);
        if (it != cache.end()) {
            bpm = it->second.bpm;
            offset = it->second.offset;
            return true;
        }
        return false;
    }

    static std::string computeHash(const std::string& filepath) {
        std::error_code ec;
        std::filesystem::path absPath = std::filesystem::absolute(filepath, ec);
        if (ec) {
            absPath = filepath;
        }
        
        auto fileSize = std::filesystem::file_size(absPath, ec);
        if (ec) return "";

        std::string normPath = absPath.generic_string();
        std::string filename = absPath.filename().string();

#if defined(_WIN32)
        std::transform(normPath.begin(), normPath.end(), normPath.begin(), [](unsigned char c) {
            return std::tolower(c);
        });
        std::transform(filename.begin(), filename.end(), filename.begin(), [](unsigned char c) {
            return std::tolower(c);
        });
#endif

        std::string key = normPath + "|" + filename + "|" + std::to_string(fileSize);

        // FNV-1a 64-bit over path+filename+size — fast, no file read needed
        uint64_t hash = 14695981039346656037ULL;
        for (unsigned char c : key) {
            hash ^= c;
            hash *= 1099511628211ULL;
        }

        char buf[17];
        std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)hash);
        return std::string(buf);
    }

private:
    std::string dbFile;
    std::unordered_map<std::string, AnalysisData> cache;
    std::mutex dbMutex;
};
