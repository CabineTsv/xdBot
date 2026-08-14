#include "macro_conversion.hpp"

#include "../gdr/xb_format.hpp"

using namespace geode::prelude;

namespace macro_conversion {
namespace {
std::string extensionForFormat(SaveFormat format) {
    switch (format) {
    case SaveFormat::GDR2:
        return ".gdr2";
    case SaveFormat::GDR1:
        return ".gdr";
    case SaveFormat::JSON:
        return ".gdr.json";
    case SaveFormat::XB:
        return ".xb";
    }

    return ".gdr2";
}

std::filesystem::path nextAvailablePath(std::filesystem::path path) {
    if (!std::filesystem::exists(path))
        return path;

    auto stem = geode::utils::string::pathToString(path.stem());
    auto extension = geode::utils::string::pathToString(path.extension());

    if (extension == ".json" && path.stem().extension() == ".gdr") {
        stem = geode::utils::string::pathToString(path.stem().stem());
        extension = ".gdr.json";
    }

    int index = 1;
    while (true) {
        auto candidate = path.parent_path() / fmt::format("{} ({}){}", stem, index++, extension);
        if (!std::filesystem::exists(candidate))
            return candidate;
    }
}

gdr::Result<std::vector<uint8_t>> exportLegacyXDAsGdr2(BotReplay& replay) {
    return replay.exportGDR2();
}
} // namespace

LoadResult load(std::filesystem::path const& path) {
    LoadResult result;
    result.legacyXD = path.extension() == ".xd";

    if (result.legacyXD) {
        result.replay = gdr_legacy::importXD(path);
        return result;
    }

    auto readResult = geode::utils::file::readBinary(path);
    if (readResult.isErr()) {
        log::error("Failed to read macro file {}: {}", path, readResult.unwrapErr());
        return result;
    }

    auto data = readResult.unwrap();
    result.replay = importData(data);
    return result;
}

BotReplay importData(std::vector<uint8_t>& data) {
    return gdr_legacy::importReplay(data);
}

gdr::Result<std::vector<uint8_t>> exportData(BotReplay& replay, SaveFormat format) {
    switch (format) {
    case SaveFormat::GDR2:
        return replay.exportGDR2();
    case SaveFormat::GDR1:
        return gdr::Ok<std::vector<uint8_t>>(replay.exportGDR1());
    case SaveFormat::JSON:
        return gdr::Ok<std::vector<uint8_t>>(replay.exportJSON());
    case SaveFormat::XB:
        return gdr::Ok<std::vector<uint8_t>>(xb_format::exportXB(replay));
    }

    return gdr::Err<std::vector<uint8_t>>("Unknown save format");
}

int save(BotReplay& replay,
         std::string const& author,
         std::string const& description,
         std::filesystem::path const& path,
         SaveFormat format) {
    if (replay.inputs.empty())
        return 31;

    auto finalPath = nextAvailablePath(path.string() + extensionForFormat(format));
    log::debug("Saving macro to path: {}", finalPath);

    replay.author = author;
    replay.description = description;
    replay.duration = static_cast<float>(replay.inputs.back().frame) / replay.framerate;

    auto dataResult = exportData(replay, format);
    if (dataResult.isErr()) {
        log::error("Macro export failed: {}", dataResult.unwrapErr());
        return 23;
    }

    auto data = std::move(dataResult).unwrap();
    if (data.empty())
        return 23;

    auto writeResult = geode::utils::file::writeBinary(finalPath, data);
    if (writeResult.isErr()) {
        log::error("Failed to write file: {}", writeResult.unwrapErr());
        return 20;
    }

    return 0;
}

Result<ImportResult> importFile(std::filesystem::path const& sourcePath,
                                std::filesystem::path const& targetDirectory) {
    std::filesystem::create_directories(targetDirectory);

    auto targetPath = nextAvailablePath(targetDirectory / sourcePath.filename());
    bool legacyXD = sourcePath.extension() == ".xd";
    if (!legacyXD) {
        std::error_code ec;
        std::filesystem::copy_file(
            sourcePath, targetPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            return Err(fmt::format("Failed to copy macro file: {}", ec.message()));

        return Ok(ImportResult{targetPath, false});
    }

    auto loadResult = load(sourcePath);
    if (loadResult.replay.description == "fail")
        return Err("Failed to load legacy .xd macro");

    targetPath.replace_extension(".gdr2");
    targetPath = nextAvailablePath(targetPath);

    auto exportResult = exportLegacyXDAsGdr2(loadResult.replay);
    if (exportResult.isErr())
        return Err(exportResult.unwrapErr());

    auto writeResult = geode::utils::file::writeBinary(targetPath, exportResult.unwrap());
    if (writeResult.isErr())
        return Err(fmt::format("Failed to write macro file: {}", writeResult.unwrapErr()));

    return Ok(ImportResult{targetPath, true});
}
} // namespace macro_conversion
