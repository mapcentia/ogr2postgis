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
#include "ogrsf_frmts.h"
#include "thread_pool.hpp"
#include "gdal_utils.h"


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
     * @param a
     * @param b
     * @return
     */
    inline bool caseInsCharCompareN(char a, char b);

    /**
     *
     * @param s1
     * @param s2
     * @return
     */
    inline bool caseInsCompare(const std::string &s1, const std::vector<std::string> &s2) {
        for (std::string text: s2) {
            if (s1.size() == text.size() && equal(s1.begin(), s1.end(), text.begin(), caseInsCharCompareN)) {
                return true;
            }
        }
        return false;
    }

    /**
     *
     * @param a
     * @param b
     * @return
     */
    inline bool caseInsCharCompareN(char a, char b) {
        return toupper(a) == toupper(b);
    }

    inline std::string getGeomType(int t) {
        std::string type;
        switch (t) {
            case 1:
                type = "point";
                break;
            case 2:
                type = "linestring";
                break;
            case 3:
                type = "polygon";
                break;
            case 4:
                type = "multipoint";
                break;
            case 5:
                type = "multilinestring";
                break;
            case 6:
                type = "multipolygon";
            default: ;
        }
        return type;
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
              const std::function<void ((layer l))> &callback, GDALDatasetH pgDs);

    /**
     *
     * @param e
     * @param n
     * @param msg
     */
    static void pgErrorHandler(CPLErr e, CPLErrorNum n, const char *msg) {
        std::string str(msg);
        std::erase(str, '\n');
        ctx *myctx = static_cast<ctx *>(CPLGetErrorHandlerUserData());
        layers[myctx->layerIndex].error = str;
        myctx->error = true;
    }

    /**
     *
     * @param e
     * @param n
     * @param msg
     */
    static void openErrorHandler(CPLErr e, CPLErrorNum n, const char *msg) {
        std::string str(msg);
        auto *l = static_cast<layer *>(CPLGetErrorHandlerUserData());
        l->error = str;
    }

    /**
     *
     * @param file
     * @param extension
     * @param callback
     */
    inline void openSource(std::string &file, const std::string &extension,
                           const std::function<void ((layer l))> &callback) {
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

        std::mutex open_mtx;

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
            layers.emplace_back(std::move(l));
            callback(l);
            CPLPopErrorHandler();
            return;
        }
        const OGRSpatialReference *projection;
        char *wktString{nullptr};
        const char *authorityName;
        const char *authorityCode;
        std::string authStr;
        int layerCount{poDS->GetLayerCount()};
        std::string hasWkt{"True"};
        std::string layerName;
        std::string driverName{poDS->GetDriverName()};
        for (int i = 0; i < layerCount; i++) {
            OGRLayer *layer{poDS->GetLayer(i)};
            const OGRSpatialReference *reference = layer->GetSpatialRef();
            if (reference != nullptr) {
                projection = layer->GetLayerDefn()->OGRFeatureDefn::GetGeomFieldDefn(0)->GetSpatialRef();
                projection->exportToWkt(&wktString);
                authorityName = projection->GetAuthorityName(nullptr);
                authorityCode = projection->GetAuthorityCode(nullptr);
                if (authorityName != nullptr && authorityCode != nullptr) {
                    authStr = std::string(authorityName) + ":" + std::string(authorityCode);
                } else {
                    authStr = "-";
                }
            } else {
                authorityName = "";
                authorityCode = "Na";
                hasWkt = "False";
                authStr = "-";
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
                driverName, featureCount, type, poDS->GetLayer(i)->GetName(), hasWkt, file,
                wktString == nullptr ? "" : std::string(wktString),
                authStr, i, "", singleMultiMixed
            }; {
                std::lock_guard<std::mutex> lock(open_mtx);
                callback(l);
                layers.emplace_back(std::move(l));
            }
            if (wktString != nullptr) {
                CPLFree(wktString);
                wktString = nullptr;
            }
        }
        CPLPopErrorHandler();
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
        const std::function<void ((std::vector<std::string> fileNames))> &callback1,
        const std::function<void ((layer l))> &callback2,
        const std::function<void ((std::vector<layer> layers))> &callback3,
        const std::function<void ((layer l))> &callback4
    ) {
        GDALAllRegister();

        static const std::vector<std::string> extensions{
            ".tab", ".shp", ".gml", ".geojson", ".gpkg",
            ".gdb", ".fgb", ".csv", ".txt"
        };

        // 1) shared vector + mutex
        std::vector<std::pair<std::string, std::string> > fileEntries;
        std::mutex entriesMtx;

        // 2) recursive scan function
        std::function<void(const std::string &)> scanDirectory =
                [&](const std::string &dirPath) {
            for (auto &p: std::filesystem::directory_iterator(dirPath)) {
                if (p.is_directory()) {
                    // schedule a new scan task
                    pool.push_task(scanDirectory, p.path().string());
                } else {
                    auto ext = p.path().extension().string();
                    if (caseInsCompare(ext, extensions)) {
                        // record file + its extension
                        std::lock_guard lock(entriesMtx);
                        fileEntries.emplace_back(p.path().string(), ext);
                    }
                }
            }
        };

        // 3) kick off the first scan (or just treat .gdb specially)
        if (std::filesystem::is_regular_file(path)) {
            // If it's a file, add to fileEntries
            fileEntries.emplace_back(path, std::filesystem::path(path).extension().string());
        } else if (path.find(".gdb") != std::string::npos) {
            // Handle .gdb case
            fileEntries.emplace_back(path, "");
        } else {
            // Otherwise, process the directory
            pool.push_task(scanDirectory, path);
            pool.wait_for_tasks();
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

        int i{0};
        // Import in PostGIS
        if (config.import) {
            setenv("PGCLIENTENCODING", "UTF8", 1);
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
            callback3(layers);
            for (const layer &l: layers) {
                if (l.error.empty()) {
                    pool.push_task(translate, config, l, "UTF8", i, true, callback4, pgDsUTF8.get());
                } else {
                    callback4(l);
                }
                i++;
            };
            pool.wait_for_tasks();
        }
        OGRCleanupAll();
        return layers;
    }

    inline void
    translate(const config &config, layer l, const std::string &encoding, const int index, const bool first,
              const std::function<void ((layer l))> &callback, GDALDatasetH pgDs) {
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

            setenv("PGCLIENTENCODING", config.fallbackEncoding.c_str(), 1);
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
