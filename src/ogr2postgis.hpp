/*
 * @author     Martin Høgh <mh@mapcentia.com>
 * @copyright  2013-2024 MapCentia ApS
 * @license    http://www.gnu.org/licenses/#AGPL  GNU AFFERO GENERAL PUBLIC LICENSE 3
 */
#pragma once

#include <list>
#include <filesystem>
#include <iostream>
#include <vector>
#include "gdal/ogrsf_frmts.h"
#include "thread_pool.hpp"
#include "gdal/gdal_utils.h"


namespace ogr2postgis {
    inline std::mutex mtx;
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
        bool json;
        bool autodetect;
        std::string extension;
    };

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
            if ((s1.size() == text.size()) && equal(s1.begin(), s1.end(), text.begin(), caseInsCharCompareN))
                return true;
        }
        return false;
    }

    /**
     *
     * @param a
     * @param b
     * @return
     */
    bool caseInsCharCompareN(char a, char b) {
        return (toupper(a) == toupper(b));
    }

    /**
     *
     * @param t
     * @return
     */
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

    inline std::vector<layer> layers;

    struct ctx {
        int layerIndex{};
        bool error{false};
    };

    /**
     *
     * @param config
     * @param l
     * @param encoding
     * @param index
     * @param first
     * @param callback
     */
    void
    translate(const config &config, layer l, const std::string &encoding, int index, bool first,
              const std::function<void ((layer l))> &callback);

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
     * @param callback
     */
    inline void openSource(const std::string &file, const std::function<void ((layer l))> &callback) {
        layer l = {
            "", 0, "", "", "", file, "",
            "", 0, "", false
        };
        CPLPushErrorHandlerEx(&openErrorHandler, &l);
        auto *poDS = static_cast<GDALDataset *>(GDALOpenEx(file.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
        if (!l.error.empty() || poDS == nullptr) {
            l.error = !l.error.empty() ? l.error : "Unable to open file";
            std::lock_guard<std::mutex> lock(mtx);
            layers.push_back(l);
            callback(l);
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
            OGRFeature *poFeature;
            typeFromLayer = getGeomType(layer->GetGeomType());
            while ((poFeature = layer->GetNextFeature()) != nullptr) {
                OGRGeometry *poGeometry = poFeature->GetGeometryRef();
                if (poGeometry != nullptr) {
                    typeDeteced = getGeomType(wkbFlatten(poGeometry->getGeometryType()));
                }
                count++;
                if (count == maxFeatures || count == featureCount) {
                    break;
                }
                if (!tmpType.empty() &&
                    (tmpType != typeDeteced && tmpType != "multi" + typeDeteced &&
                     tmpType != typeDeteced.substr(5, typeDeteced.length()))) {
                    typeDeteced = "geometry";
                    break;
                }
                if (!tmpType.empty() &&
                    (tmpType == "multi" + typeDeteced || tmpType == typeDeteced.substr(5, typeDeteced.length()))) {
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
                std::lock_guard<std::mutex> lock(mtx);
                layers.push_back(l);
                callback(l);
            }
            OGRFeature::DestroyFeature(poFeature);
        }
        GDALClose(poDS);
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
        std::vector<std::string> extensions{{".tab", ".shp", ".gml", ".geojson", ".gpkg", ".gdb", ".fgb", ".csv"}};
        std::vector<std::string> fileNames;
        if (path.find(".gdb") != std::string::npos) {
            fileNames.push_back(path);
        } else {
            try {
                for (auto &p: std::filesystem::recursive_directory_iterator(path)) {
                    if (!config.nln.empty() && config.import && !config.append) {
                        printf(
                            "ERROR: Can't use alternative table name for importing directories. All tables will be named alike.\n");
                        exit(1);
                    }
                    std::string file = p.path().string();
                    std::string fileExtension = p.path().extension().string();
                    if (caseInsCompare(fileExtension, extensions)) {
                        fileNames.push_back(file);

                        std::transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(),
                                       [](unsigned char c) { return std::tolower(c); });
                        config.extension = fileExtension;
                    }
                }
            } catch (const std::exception &e) {
                if (!std::filesystem::exists(path)) {
                    printf("ERROR: Could not open directory or file.\n");
                    exit(1);
                };
                fileNames.push_back(path);
                const std::filesystem::path p = std::filesystem::path(path);
                config.extension = p.extension();
            }
        }
        callback1(fileNames);
        for (const std::string &fileName: fileNames) {
            pool.push_task(openSource, fileName, callback2);
        }
        pool.wait_for_tasks();
        int i{0};
        // Import in PostGIS
        if (config.import) {
            callback3(layers);
            for (const layer &l: layers) {
                if (l.error.empty()) {
                    pool.push_task(translate, config, l, "UTF8", i, true, callback4);
                } else {
                    callback4(l);
                }
                i++;
            };
            pool.wait_for_tasks();
        }
        return layers;
    }

    inline void
    translate(const config &config, layer l, const std::string &encoding, const int index, const bool first,
              const std::function<void ((layer l))> &callback) {
        char **argv{nullptr};
        std::string altName = l.layerName;
        std::string env = "PGCLIENTENCODING=" + encoding;
        ctx myctx = {
            .layerIndex = index,
            .error = false,
        };
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
        const char *targetSrs = !l.wktString.empty()
                                    ? l.wktString.c_str()
                                    : config.s_srs.c_str();
        if (targetSrs == nullptr) {
            layers[index].error = "Can't impoort without source srs";
            CSLDestroy(argv);
            callback(l);
            return;
        }
        argv = CSLAddString(argv, "-f");
        argv = CSLAddString(argv, "PostgreSQL");
        if (config.append) {
            argv = CSLAddString(argv, "-update");
            argv = CSLAddString(argv, "-append");
        }
        argv = CSLAddString(argv, "-overwrite");
        // argv = CSLAddString(argv, "-skipfailures");
        argv = CSLAddString(argv, "-lco");
        argv = CSLAddString(argv, "FID=gid");

        argv = CSLAddString(argv, "-nln");
        argv = CSLAddString(argv, altName.c_str());

        // Geom related flags
        if (!l.type.empty()) {
            argv = CSLAddString(argv, "-nlt");
            argv = CSLAddString(argv, l.type.c_str());
            argv = CSLAddString(argv, "-lco");
            argv = CSLAddString(argv, "PRECISION=NO");
            argv = CSLAddString(argv, "-lco");
            argv = CSLAddString(argv, "GEOMETRY_NAME=the_geom");
            argv = CSLAddString(argv, "-s_srs"); // source projection
            argv = CSLAddString(argv, targetSrs);
            argv = CSLAddString(argv, "-t_srs");
            argv = CSLAddString(argv,
                                strcmp(l.authStr.c_str(), "-") != 0
                                    ? l.authStr.c_str()
                                    : !config.t_srs.empty()
                                          ? config.t_srs.c_str()
                                          : "EPSG:4326");
        }

        argv = CSLAddString(argv, l.layerName.c_str());


        GDALDatasetH pgDs = GDALOpenEx(config.connection.c_str(),
                                       GDAL_OF_UPDATE | GDAL_OF_VECTOR | GDAL_OF_VERBOSE_ERROR,
                                       nullptr, nullptr, nullptr);

        char **papszOptions = nullptr;
        if (config.extension == ".csv") {
            papszOptions = CSLAddNameValue(papszOptions, "AUTODETECT_TYPE", config.autodetect ? "YES" : "NO");
        }

        GDALDatasetH sourceDs = GDALOpenEx(l.file.c_str(), GDAL_OF_VECTOR, nullptr, papszOptions, nullptr);

        int bUsageError{FALSE};
        GDALVectorTranslateOptions *opt = GDALVectorTranslateOptionsNew(argv, nullptr);
        auto *dst = static_cast<GDALDataset *>(GDALVectorTranslate(nullptr, pgDs, 1, &sourceDs, opt, &bUsageError));
        GDALVectorTranslateOptionsFree(opt);
        GDALClose(dst);
        CSLDestroy(argv);
        // If error we try with the fallback encoding
        if (myctx.error && first) {
            layers[index].error = "";
            translate(config, l, config.fallbackEncoding, index, false, callback);
            return;
        }
        callback(l);
    }
}
