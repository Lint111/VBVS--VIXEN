// closure_manifest merge  --repo <git root> --build <ninja dir> --exe <ninja target of the capture executable>
//                         --out <manifest.tsv> --detail <closure.tsv> <family>=<recording dir>...
// closure_manifest select --manifest <manifest.tsv> <changed repo-relative path>...   (one family per line)
#include "closure_manifest.h"

#include <cstdio>
#include <iostream>

namespace fs = std::filesystem;
using namespace Vixen::Closure;

namespace {
std::vector<std::string> Query(const std::string& command) {
    std::vector<std::string> lines;
    if (FILE* pipe = popen(command.c_str(), "r")) {
        char buffer[8192];
        std::string line;
        while (std::fgets(buffer, sizeof buffer, pipe)) {
            line = buffer;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
            lines.push_back(line);
        }
        pclose(pipe);
    }
    return lines;
}

std::string Quote(const std::string& text) { return "'" + text + "'"; }

// Build-graph inputs of one artifact: explicit/implicit inputs and compiler-reported dependencies.
std::set<std::string> GraphInputs(const std::string& build, const std::string& artifact) {
    std::set<std::string> inputs;
    const std::string base = "ninja -C " + Quote(build) + " -t ";
    for (const auto& line : Query(base + "inputs -- " + Quote(artifact) + " 2>/dev/null")) inputs.insert(line);
    for (const auto& line : Query(base + "deps -- " + Quote(artifact) + " 2>/dev/null")) {
        if (line.rfind("    ", 0) == 0) inputs.insert(line.substr(4));
    }
    return inputs;
}

// Tracked files of the repository, repository-relative: the only files whose change a lane can make.
std::set<std::string> TrackedFiles(const std::string& repo) {
    const auto lines = Query("git -C " + Quote(repo) + " ls-files 2>/dev/null");
    return {lines.begin(), lines.end()};
}

bool Under(const fs::path& path, const fs::path& root) {
    const auto rel = path.lexically_relative(root);
    return !rel.empty() && *rel.begin() != "..";
}

int Merge(const std::vector<std::string>& args) {
    std::string repoArg, buildArg, exe, out, detail;
    std::map<std::string, std::vector<fs::path>> recordings;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto value = [&]() -> const std::string& { return args.at(++i); };
        if (args[i] == "--repo") repoArg = value();
        else if (args[i] == "--build") buildArg = value();
        else if (args[i] == "--exe") exe = value();
        else if (args[i] == "--out") out = value();
        else if (args[i] == "--detail") detail = value();
        else if (const auto eq = args[i].find('='); eq != std::string::npos)
            recordings[args[i].substr(0, eq)].push_back(args[i].substr(eq + 1));
    }
    if (repoArg.empty() || buildArg.empty() || out.empty() || recordings.empty()) {
        std::cerr << "merge: --repo, --build, --out and at least one <family>=<dir> are required\n";
        return 2;
    }
    const fs::path repo = fs::weakly_canonical(repoArg), build = fs::weakly_canonical(buildArg);
    Manifest manifest;
    const auto tracked = TrackedFiles(repo.string());
    const auto Add = [&](const fs::path& source, const std::string& family) {
        const std::string rel = source.lexically_relative(repo).string();
        if (!tracked.count(rel)) return false;  // run outputs and fetched third-party trees are not lane inputs
        manifest.Add(rel, family);
        return true;
    };
    std::ofstream detailOut(detail.empty() ? "/dev/null" : detail);
    detailOut << "family\topened\thash\tsource\n";
    std::map<std::string, std::set<std::string>> graphCache;  // artifact -> sources
    Families all;
    for (const auto& [family, dirs] : recordings) {
        all.insert(family);
        std::set<std::string> opened;
        for (const auto& dir : dirs) {
            if (!fs::is_directory(dir)) { std::cerr << "merge: missing recording dir " << dir << "\n"; return 3; }
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (entry.path().extension() != ".paths") continue;
                std::ifstream in(entry.path());
                for (std::string line; std::getline(in, line);) if (!line.empty()) opened.insert(line);
            }
        }
        for (const auto& raw : opened) {
            std::error_code ec;
            const fs::path path = fs::weakly_canonical(raw, ec);
            if (ec || !fs::is_regular_file(path, ec)) continue;
            if (Under(path, build)) {
                // A build-tree file the graph does not produce is a run output (captures, caches): not an input.
                const std::string rel = path.lexically_relative(build).string();
                auto [slot, fresh] = graphCache.try_emplace(rel);
                if (fresh) slot->second = GraphInputs(build.string(), rel);
                const auto hash = ContentHash(path);
                for (const auto& input : slot->second) {
                    const fs::path source = fs::weakly_canonical(fs::path(input).is_absolute() ? fs::path(input) : build / input);
                    if (!Under(source, repo) || Under(source, build)) continue;
                    if (!Add(source, family)) continue;
                    detailOut << family << '\t' << rel << '\t' << std::hex << hash << std::dec << '\t'
                              << source.lexically_relative(repo).string() << '\n';
                }
            } else if (Under(path, repo)) {
                if (!Add(path, family)) continue;
                detailOut << family << '\t' << path.lexically_relative(repo).string() << '\t' << std::hex
                          << ContentHash(path) << std::dec << '\t' << path.lexically_relative(repo).string() << '\n';
            }
        }
    }
    // The capture executable is shared by every family and statically links the renderer: its own sources
    // can change any family, so they map to all of them. Closures narrow only what is loaded at run time.
    if (!exe.empty()) {
        for (const auto& input : GraphInputs(build.string(), exe)) {
            std::error_code ec;
            const fs::path source = fs::weakly_canonical(fs::path(input).is_absolute() ? fs::path(input) : build / input, ec);
            if (ec || !Under(source, repo) || Under(source, build)) continue;
            if (source.extension() == ".o" || source.extension() == ".a") continue;
            if (tracked.count(source.lexically_relative(repo).string()))
                manifest.AddAll(source.lexically_relative(repo).string(), all);
        }
    }
    std::ofstream(out) << manifest.Serialize();
    std::cout << "families=" << all.size() << " sources=" << manifest.Rows().size() << "\n";
    return 0;
}

int Select(const std::vector<std::string>& args) {
    std::string manifestPath;
    std::vector<std::string> changed;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--manifest") manifestPath = args.at(++i);
        else changed.push_back(args[i]);
    }
    std::ifstream in(manifestPath);
    if (!in) { std::cerr << "select: cannot read manifest " << manifestPath << "\n"; return 2; }
    for (const auto& family : Manifest::Parse(in).Select(changed)) std::cout << family << '\n';
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && args[0] == "merge") return Merge({args.begin() + 1, args.end()});
    if (!args.empty() && args[0] == "select") return Select({args.begin() + 1, args.end()});
    std::cerr << "usage: closure_manifest merge|select ...\n";
    return 2;
}
