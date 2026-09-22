/*
 * @author     Martin Høgh <mh@mapcentia.com>
 * @copyright  2013-2025 MapCentia ApS
 * @license    http://www.gnu.org/licenses/#AGPL  GNU AFFERO GENERAL PUBLIC LICENSE 3
 */
#pragma once

#include <list>
#include <filesystem>
#include <iostream>
#include <vector>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <memory>
#include <algorithm>
#include <cctype>
#include "ogrsf_frmts.h"
#include "thread_pool.hpp"
#include "gdal_utils.h"
#include "gdal_rat.h"
#include "ogr_srs_api.h"
#include <cstdlib>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ogr2postgis {
    inline BS::thread_pool pool;

    struct config {
        std::string connection;
        std::string t_srs;
        std::string s_srs;
        std::string nln;
        std::string schema;
        std::string fallbackEncoding;
        bool import{false};
        bool p_multi{false};
        bool append{false};
        bool truncate{false};
        bool json;
        bool autodetect;
        std::string extension;
        std::string timestamp;
        std::string x_possible_names;
        std::string y_possible_names;
    };

    struct layer {
        std::string driverName;
        GIntBig featureCount;
        std::string type;
        std::string layerName;
        std::string hasWkt;
        std::string file;
        std::string wktString;
        std::string authStr;
        int layerIndex;
        std::string error;
        bool singleMultiMixed;
    };

    struct ctx {
        int layerIndex{};
        bool error{false};
    };

    constexpr int OPEN_FLAGS = GDAL_OF_VECTOR; // Dataset open mode constant

    // Define a smart pointer for GDALDataset with a custom deleter
    using GDALDatasetPtr = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;

    /**
     * Returns the directory containing the running executable, or an empty
     * path if it cannot be determined.
     */
    inline std::filesystem::path executableDir() {
#ifdef _WIN32
        wchar_t buf[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return {};
        return std::filesystem::path(buf).parent_path();
#else
        std::error_code ec;
        const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (ec) return {};
        return exe.parent_path();
#endif
    }

    /**
     * Points GDAL and PROJ at the data directories shipped next to the
     * executable ("gdal-data" and "proj"). Windows builds of GDAL/PROJ have no
     * compiled-in data path, so without this GML files cannot be parsed and
     * EPSG codes cannot be resolved. GDAL_DATA and PROJ_DATA/PROJ_LIB set in
     * the environment take precedence. Must be called before GDALAllRegister().
     */
    inline void configureDataPaths() {
        const std::filesystem::path exeDir = executableDir();
        if (exeDir.empty()) return;
        std::error_code ec;

        if (CPLGetConfigOption("GDAL_DATA", nullptr) == nullptr) {
            const auto gdalData = exeDir / "gdal-data";
            if (std::filesystem::is_directory(gdalData, ec)) {
                CPLSetConfigOption("GDAL_DATA", gdalData.string().c_str());
            }
        }

        if (std::getenv("PROJ_DATA") == nullptr && std::getenv("PROJ_LIB") == nullptr) {
            const auto projData = exeDir / "proj";
            if (std::filesystem::is_directory(projData, ec)) {
                const std::string dir = projData.string();
                const char *paths[] = {dir.c_str(), nullptr};
                OSRSetPROJSearchPaths(paths);
            }
        }
    }

    // Note: On POSIX systems, gmtime_r is thread-safe.
    // On Windows, you might need to use gmtime_s.
    inline std::string getCurrentTimestamp() {
        using namespace std::chrono;
        // Get current time_point and convert to time_t for seconds.
        const auto now = system_clock::now();
        auto now_time_t = system_clock::to_time_t(now);
        // Extract microseconds from the current time.
        auto micros = duration_cast<microseconds>(now.time_since_epoch()) % 1000000;
        // Convert time_t to a tm structure in UTC.
        std::tm utc_tm{};
#if defined(_WIN32) || defined(_WIN64)
        gmtime_s(&utc_tm, &now_time_t);  // Windows thread-safe version
#else
        gmtime_r(&now_time_t, &utc_tm); // POSIX thread-safe version
#endif
        // Build the string in the desired format.
        std::ostringstream oss;
        oss << std::put_time(&utc_tm, "%Y-%m-%d %H:%M:%S")
                << '.' << std::setw(6) << std::setfill('0') << micros.count()
                << "+00";
        return oss.str();
    }

    /**
     *
     * @param s
     * @return
     */
    inline std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](const unsigned char c) { return std::tolower(c); });
        return s;
    }

    /**
     * Maps an OGR geometry type to the PostGIS type name used for -nlt. Z/M
     * flags are stripped and ISO curve types map to their linear counterparts,
     * which ogr2ogr linearizes on import.
     */
    inline std::string getGeomType(int t) {
        switch (wkbFlatten(t)) {
            case wkbPoint:
                return "point";
            case wkbLineString:
            case wkbCircularString:
            case wkbCompoundCurve:
                return "linestring";
            case wkbPolygon:
            case wkbCurvePolygon:
                return "polygon";
            case wkbMultiPoint:
                return "multipoint";
            case wkbMultiLineString:
            case wkbMultiCurve:
                return "multilinestring";
            case wkbMultiPolygon:
            case wkbMultiSurface:
                return "multipolygon";
            default:
                return "";
        }
    }

    constexpr int maxFeatures{1000};

    inline std::vector<layer> layers;

    /**
     *
     * @param config
     * @param l
     * @param encoding
     * @param index
     * @param first
     * @param callback
     * @param pgDs
     */
    void
    translate(const config &config, layer l, const std::string &encoding, int index, bool first,
              const std::function<void(layer)> &callback, GDALDatasetH pgDs);

    /**
     *
     * @param e
     * @param n
     * @param msg
     */
    static void pgErrorHandler(CPLErr e, CPLErrorNum n, const char *msg) {
        if (e < CE_Failure) return; // Debug and warning messages are not errors
        std::string str(msg);
        std::erase(str, '\n');
        ctx *myctx = static_cast<ctx *>(CPLGetErrorHandlerUserData());
        // Ignore error regarding Layer creation options when appending
        const std::string prefix = "Layer creation options ignored";
        if (!str.starts_with(prefix)) {
            layers[myctx->layerIndex].error = str;
            myctx->error = true;
        }
    }

    /**
     *
     * @param e
     * @param n
     * @param msg
     */
    static void openErrorHandler(CPLErr e, CPLErrorNum n, const char *msg) {
        if (e < CE_Failure) return; // Debug and warning messages are not errors
        auto *l = static_cast<layer *>(CPLGetErrorHandlerUserData());
        l->error = std::string(msg);
    }

    /**
     *
     * @param file
     * @param extension
     * @param callback
     */
    inline void openSource(std::string &file, const std::string &extension,
                           const std::function<void(layer)> &callback) {
        layer l = {
            "", 0, "", "", "", file, "",
            "", 0, "", false
        };
        CPLPushErrorHandlerEx(&openErrorHandler, &l);

        // If txt file, we think it's a CSV
        std::string f;
        if (extension == ".txt") {
            f = "CSV:" + file;
        } else {
            f = file;
        }

        static std::mutex open_mtx;

        GDALDatasetPtr poDS(
            static_cast<GDALDataset *>(GDALOpenEx(
                f.c_str(), // File path
                OPEN_FLAGS, // Dataset open mode
                nullptr, // Driver list
                nullptr, // Open-specific options
                nullptr // Sibling files
            )),
            &GDALClose // Custom deleter ensures proper cleanup
        );

        if (!l.error.empty() || poDS == nullptr) {
            l.error = !l.error.empty() ? l.error : "Unable to open file";
            std::lock_guard<std::mutex> lock(open_mtx);
            callback(l);
            layers.emplace_back(std::move(l));
            CPLPopErrorHandler();
            return;
        }
        int layerCount{poDS->GetLayerCount()};
        std::string driverName{poDS->GetDriverName()};
        for (int i = 0; i < layerCount; i++) {
            OGRLayer *layer{poDS->GetLayer(i)};
            std::string hasWkt{"True"};
            std::string authStr{"-"};
            std::string wktString;
            OGRSpatialReference featureSrs;
            const OGRSpatialReference *reference = layer->GetSpatialRef();
            if (reference == nullptr) {
                // Some formats (e.g. GML with srsName only on the geometries)
                // carry the CRS on the features rather than on the layer.
                layer->ResetReading();
                if (OGRFeature *first = layer->GetNextFeature(); first != nullptr) {
                    const OGRGeometry *g = first->GetGeometryRef();
                    if (g != nullptr && g->getSpatialReference() != nullptr) {
                        featureSrs = *g->getSpatialReference();
                        reference = &featureSrs;
                    }
                    OGRFeature::DestroyFeature(first);
                }
                layer->ResetReading();
            }
            if (reference != nullptr) {
                char *wkt{nullptr};
                reference->exportToWkt(&wkt);
                if (wkt != nullptr) {
                    wktString = wkt;
                    CPLFree(wkt);
                }
                const char *authorityName = reference->GetAuthorityName(nullptr);
                const char *authorityCode = reference->GetAuthorityCode(nullptr);
                if (authorityName != nullptr && authorityCode != nullptr) {
                    authStr = std::string(authorityName) + ":" + authorityCode;
                }
            } else {
                hasWkt = "False";
            }
            // Count features
            GIntBig featureCount = layer->GetFeatureCount(1);
            int count{0};
            std::string type;
            std::string typeDeteced;
            std::string typeFromLayer;
            std::string tmpType;
            bool singleMultiMixed{false};
            typeFromLayer = getGeomType(layer->GetGeomType());

            struct OGRFeatureDeleter {
                void operator()(OGRFeature *feature) const {
                    if (feature) {
                        OGRFeature::DestroyFeature(feature);
                    }
                }
            };
            OGRFeature *raw;
            while ((raw = layer->GetNextFeature()) != nullptr) {
                // Retrieve the next feature and wrap it immediately.
                std::unique_ptr<OGRFeature, OGRFeatureDeleter> poFeature(raw);
                if (!poFeature) {
                    CPLPopErrorHandler();
                    break; // No more features available.
                }

                OGRGeometry *poGeometry = poFeature->GetGeometryRef();
                if (poGeometry != nullptr) {
                    typeDeteced = getGeomType(wkbFlatten(poGeometry->getGeometryType()));
                }
                count++;
                if (count == maxFeatures || count == featureCount) {
                    // RAII ensures that 'poFeature' is cleaned up.
                    CPLPopErrorHandler();
                    break;
                }
                if (!tmpType.empty() &&
                    (tmpType != typeDeteced && tmpType != "multi" + typeDeteced &&
                     tmpType != typeDeteced.substr(5, typeDeteced.length()))) {
                    typeDeteced = "geometry";
                    CPLPopErrorHandler();
                    break;
                }
                if (!tmpType.empty() &&
                    (tmpType == "multi" + typeDeteced ||
                     tmpType == typeDeteced.substr(5, typeDeteced.length()))) {
                    singleMultiMixed = true;
                }
                tmpType = typeDeteced;
            }

            if (singleMultiMixed || typeFromLayer.empty()) {
                type = typeDeteced;
            } else {
                type = typeFromLayer;
            }

            l = {
                driverName, featureCount, type, layer->GetName(), hasWkt, file,
                wktString, authStr, i, "", singleMultiMixed
            }; {
                std::lock_guard<std::mutex> lock(open_mtx);
                callback(l);
                layers.emplace_back(std::move(l));
            }
        }
        CPLPopErrorHandler();
    }

    /**
     * Imports the global `layers` vector into PostgreSQL. The PG datasource is
     * opened from config.connection; throws std::runtime_error if that fails.
     *
     * @param config
     * @param callback3
     * @param callback4
     */
    inline void runImport(
        const config &config,
        const std::function<void(std::vector<layer>)> &callback3,
        const std::function<void(layer)> &callback4
    ) {
        //setenv("PGCLIENTENCODING", "UTF8", 1);
        GDALDatasetPtr pgDsUTF8(
            static_cast<GDALDataset *>(
                GDALOpenEx(config.connection.c_str(),
                           GDAL_OF_UPDATE | GDAL_OF_VECTOR | GDAL_OF_VERBOSE_ERROR,
                           nullptr, nullptr, nullptr
                )
            ),
            &GDALClose // ← this calls GDALClose(ptr.get()) when the unique_ptr goes out of scope
        );

        // Safely operate on GDAL datasets
        if (pgDsUTF8 == nullptr) {
            throw std::runtime_error("Failed to open GDAL dataset");
        }
        if (callback3) callback3(layers);
        int i{0};
        for (const layer &l: layers) {
            if (l.error.empty()) {
                pool.push_task(translate, config, l, "UTF8", i, true, callback4, pgDsUTF8.get());
            } else if (callback4) {
                callback4(l);
            }
            i++;
        }
        pool.wait_for_tasks();
    }

    /**
     * Imports an explicit selection of previously analyzed layers into
     * PostgreSQL. Returns the layers with any import errors filled in.
     *
     * @param config
     * @param selection
     * @param callback3
     * @param callback4
     * @return
     */
    inline std::vector<layer> importLayers(
        const config &config,
        std::vector<layer> selection,
        const std::function<void(std::vector<layer>)> &callback3,
        const std::function<void(layer)> &callback4
    ) {
        GDALAllRegister();
        layers = std::move(selection);
        runImport(config, callback3, callback4);
        return layers;
    }

    /**
     *
     * @param config
     * @param paths
     * @param callback1
     * @param callback2
     * @param callback3
     * @param callback4
     * @return
     */
    inline std::vector<layer> start(
        config &config,
        const std::vector<std::string> &paths,
        const std::function<void(std::vector<std::string>)> &callback1,
        const std::function<void(layer)> &callback2,
        const std::function<void(std::vector<layer>)> &callback3,
        const std::function<void(layer)> &callback4
    ) {
        GDALAllRegister();
        layers.clear();

        static const std::vector<std::string> extensions{
            ".tab", ".shp", ".gml", ".geojson", ".gpkg",
            ".gdb", ".fgb", ".parquet", ".csv", ".txt"
        };

        std::vector<std::pair<std::string, std::string> > fileEntries;

        // Sequential, exception-free scan. Directory symlinks are not followed,
        // unreadable entries are skipped and .gdb directories are recorded as
        // datasets instead of being descended into.
        auto scanDirectory = [&](const std::string &root) {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
            for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                std::string ext = toLower(it->path().extension().string());
                std::error_code typeEc;
                if (it->is_directory(typeEc)) {
                    if (ext == ".gdb") {
                        fileEntries.emplace_back(it->path().string(), ext);
                        it.disable_recursion_pending();
                    }
                } else if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
                    fileEntries.emplace_back(it->path().string(), ext);
                }
            }
        };

        for (const std::string &path: paths) {
            std::error_code ec;
            const std::string ext = toLower(std::filesystem::path(path).extension().string());
            if (std::filesystem::is_regular_file(path, ec)) {
                // An explicitly given file is recorded regardless of extension
                fileEntries.emplace_back(path, ext);
            } else if (ext == ".gdb") {
                fileEntries.emplace_back(path, ext);
            } else {
                scanDirectory(path);
            }
        }

        // tell caller what we found
        std::vector<std::string> fileNamesOnly;
        fileNamesOnly.reserve(fileEntries.size());
        for (auto &fe: fileEntries) {
            fileNamesOnly.push_back(fe.first);
        }
        callback1(fileNamesOnly);

        // 4) now import each in parallel, passing along its own ext
        for (auto &fe: fileEntries) {
            pool.push_task(openSource, fe.first, fe.second, callback2);
        }
        pool.wait_for_tasks();

        // Import in PostGIS
        if (config.import) {
            runImport(config, callback3, callback4);
        }
        OGRCleanupAll();
        return layers;
    }

    /**
     *
     * @param config
     * @param path
     * @param callback1
     * @param callback2
     * @param callback3
     * @param callback4
     * @return
     */
    inline std::vector<layer> start(
        config &config,
        const std::string &path,
        const std::function<void(std::vector<std::string>)> &callback1,
        const std::function<void(layer)> &callback2,
        const std::function<void(std::vector<layer>)> &callback3,
        const std::function<void(layer)> &callback4
    ) {
        return start(config, std::vector<std::string>{path}, callback1, callback2, callback3, callback4);
    }

    inline void
    translate(const config &config, layer l, const std::string &encoding, const int index, const bool first,
              const std::function<void(layer)> &callback, GDALDatasetH pgDs) {
        char **argv{nullptr};
        std::string altName = l.layerName;
        std::string env = "PGCLIENTENCODING=" + encoding;
        ctx myctx = {
            .layerIndex = index,
            .error = false,
        };

        // Thread safety for error handling
        static std::mutex gdal_mutex;


        CPLPushErrorHandlerEx(&pgErrorHandler, &myctx);
        putenv(const_cast<char *>(env.c_str()));
        setvbuf(stdout, nullptr, _IOFBF, BUFSIZ);
        if (!config.nln.empty()) {
            altName = config.nln;
            if (l.layerIndex > 0) {
                altName = altName + "_" + std::to_string(l.layerIndex);
            }
        }
        altName = config.schema + "." + altName;

        if ((l.type == "point" || l.type == "linestring" || l.type == "polygon") &&
            (l.singleMultiMixed || config.p_multi)) {
            l.type = "multi" + l.type;
        }
        const char *sourceSrs = !l.wktString.empty()
                                    ? l.wktString.c_str()
                                    : config.s_srs.c_str();

        // Stop if no source id. Except for CSV, which we default to EPSG:4326
        if (sourceSrs == nullptr) {
            if (l.driverName != "CSV") {
                layers[index].error = "Can't import without source srs";
                CSLDestroy(argv);
                callback(l);
                CPLPopErrorHandler();
                return;
            }
            sourceSrs = "EPSG:4326";
        } {
            std::unique_lock<std::mutex> lock(gdal_mutex);

            argv = CSLAddString(argv, "-nomd");
            argv = CSLAddString(argv, "-f");
            argv = CSLAddString(argv, "PostgreSQL");
            if (config.append) {
                argv = CSLAddString(argv, "-update");
                argv = CSLAddString(argv, "-append");
            } else {
                argv = CSLAddString(argv, "-overwrite");
            }
            // Layer creation options
            argv = CSLAddString(argv, "-lco");
            argv = CSLAddString(argv, "FID=gid");
            argv = CSLAddString(argv, "-lco");
            argv = CSLAddString(argv, "PRECISION=NO");
            argv = CSLAddString(argv, "-lco");
            argv = CSLAddString(argv, "GEOMETRY_NAME=the_geom");
            // argv = CSLAddString(argv, "-skipfailures");
            argv = CSLAddString(argv, "-nln");
            argv = CSLAddString(argv, altName.c_str());
            // Geom related flags
            if (!l.type.empty()) {
                argv = CSLAddString(argv, "-nlt");
                argv = CSLAddString(argv, l.type.c_str());
            }
            argv = CSLAddString(argv, "-s_srs"); // source projection
            argv = CSLAddString(argv, sourceSrs);
            argv = CSLAddString(argv, "-t_srs");
            argv = CSLAddString(argv,
                                strcmp(l.authStr.c_str(), "-") != 0
                                    ? l.authStr.c_str()
                                    : !config.t_srs.empty()
                                          ? config.t_srs.c_str()
                                          : "EPSG:4326");
            if (!config.timestamp.empty()) {
                argv = CSLAddString(argv, "-sql");
                std::string sql = "SELECT *, CAST('" + getCurrentTimestamp() +
                                  "' AS timestamp) AS " + config.timestamp + " FROM " +
                                  l.layerName;
                argv = CSLAddString(argv, sql.c_str());
            } else {
                argv = CSLAddString(argv, l.layerName.c_str());
            }

            if (config.truncate && config.append) {
                CPLSetConfigOption("OGR_TRUNCATE", "YES");
                CPLSetConfigOption("PG_USE_COPY", "YES");
            }

            char **papszOptions = nullptr;
            if (l.driverName == "CSV") {
                papszOptions = CSLAddNameValue(papszOptions, "AUTODETECT_TYPE", config.autodetect ? "YES" : "NO");
                papszOptions = CSLAddNameValue(papszOptions, "X_POSSIBLE_NAMES", config.x_possible_names.c_str());
                papszOptions = CSLAddNameValue(papszOptions, "Y_POSSIBLE_NAMES", config.y_possible_names.c_str());
            }

            // If txt file, we think it's a CSV
            std::string f;
            if (l.driverName == "CSV") {
                f = "CSV:" + l.file;
            } else {
                f = l.file;
            }

            GDALDatasetPtr sourceDsPtr(
                static_cast<GDALDataset *>(
                    GDALOpenEx(f.c_str(),
                               GDAL_OF_VECTOR,
                               nullptr, // drivers
                               papszOptions,
                               nullptr // sibling files
                    )
                ),
                &GDALClose // ← this calls GDALClose(ptr.get()) when the unique_ptr goes out of scope
            );

            GDALDatasetH rawSourceDs = sourceDsPtr.get();

            int bUsageError{FALSE};
            GDALVectorTranslateOptions *opt = GDALVectorTranslateOptionsNew(argv, nullptr);

            GDALVectorTranslate(nullptr, pgDs, 1, &rawSourceDs, opt, &bUsageError);

            GDALVectorTranslateOptionsFree(opt);
            CSLDestroy(argv);
            CSLDestroy(papszOptions);
        }
        CPLPopErrorHandler();

        // If error we try with the fallback encoding
        if (myctx.error && first) {
            layers[index].error = "";

            //setenv("PGCLIENTENCODING", config.fallbackEncoding.c_str(), 1);
            GDALDatasetPtr pgDsFallbackEncoding(
                static_cast<GDALDataset *>(
                    GDALOpenEx(config.connection.c_str(),
                               GDAL_OF_UPDATE | GDAL_OF_VECTOR | GDAL_OF_VERBOSE_ERROR,
                               nullptr, nullptr, nullptr
                    )
                ),
                &GDALClose // ← this calls GDALClose(ptr.get()) when the unique_ptr goes out of scope
            );
            // Safely operate on GDAL datasets
            if (pgDsFallbackEncoding == nullptr) {
                throw std::runtime_error("Failed to open GDAL dataset");
            }
            translate(config, l, config.fallbackEncoding, index, false, callback, pgDsFallbackEncoding.get());
            return;
        }
        callback(l);
    }
}
