/*
 * @author     Martin Høgh <mh@mapcentia.com>
 * @copyright  2013-2023 MapCentia ApS
 * @license    http://www.gnu.org/licenses/#AGPL  GNU AFFERO GENERAL PUBLIC LICENSE 3
 */

#include <list>
#include <filesystem>
#include <iostream>
#include <vector>
#include "gdal/ogrsf_frmts.h"
#include "thread_pool.hpp"
#include "gdal/gdal_utils.h"


using namespace std;

namespace ogr2postgis {

    mutex mtx;
    BS::thread_pool pool;

    struct config {
        string connection;
        string t_srs;
        string s_srs;
        string nln;
        string schema;
        string fallbackEncoding;
        bool import{ false };
        bool p_multi{false};
        bool append{false};
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
    bool caseInsCompare(const string &s1, const vector<string> &s2) {
        for (string text: s2) {
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
    string getGeomType(int t) {
        string type;
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
        }
        return type;
    }

    const int maxFeatures{1000};
    struct layer {
        string driverName;
        GIntBig featureCount;
        string type;
        string layerName;
        string hasWkt;
        string file;
        string wktString;
        string authStr;
        int layerIndex;
        string error;
        bool singleMultiMixed;
    };

    vector<struct layer> layers;
    struct ctx {
        int layerIndex{};
        bool error{false};
    };

    /**
     *
     * @param l
     * @param encoding
     * @param index
     * @param first
     * @param callback
     */
    void
    translate(config config, layer l, const string &encoding, int index, bool first, void (*callback)(layer l));

    /**
     *
     * @param e
     * @param n
     * @param msg
     */
    static void pgErrorHandler(CPLErr e, CPLErrorNum n, const char *msg) {
        string str(msg);
        ctx *myctx = (ctx *) CPLGetErrorHandlerUserData();
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
        string str(msg);
        auto *l = (layer *) CPLGetErrorHandlerUserData();
        l->error = str;
    }

    /**
     *
     * @param file
     * @param callback
     */
    inline void openSource(string file, std::function<void ((layer l))> callback) {
        layer l = {"", 0, "", "", "", file, "",
                   "", 0, "", false};
        CPLPushErrorHandlerEx(&openErrorHandler, &l);
        auto *poDS = (GDALDataset *) GDALOpenEx(file.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
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
        string authStr;
        int layerCount{poDS->GetLayerCount()};
        string hasWkt{"True"};
        string layerName;
        string driverName{poDS->GetDriverName()};
        for (int i = 0; i < layerCount; i++) {
            OGRLayer *layer{poDS->GetLayer(i)};
            const OGRSpatialReference *reference = layer->GetSpatialRef();
            if (reference != nullptr) {
                projection = layer->GetLayerDefn()->OGRFeatureDefn::GetGeomFieldDefn(0)->GetSpatialRef();
                projection->exportToWkt(&wktString);
                authorityName = projection->GetAuthorityName(nullptr);
                authorityCode = projection->GetAuthorityCode(nullptr);
                if (authorityName != nullptr && authorityCode != nullptr) {
                    authStr = string(authorityName) + ":" + string(authorityCode);
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
            string type;
            string typeDeteced;
            string typeFromLayer;
            string tmpType;
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
                } else {
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
                    continue;
                }
            }
            if (singleMultiMixed || typeFromLayer.empty()) {
                type = typeDeteced;
            } else {
                type = typeFromLayer;
            }


            l = {driverName, featureCount, type, poDS->GetLayer(i)->GetName(), hasWkt, file,
                 wktString == nullptr ? "" : string(wktString),
                 authStr, i, "", singleMultiMixed};
            {
                std::lock_guard<std::mutex> lock(mtx);
                layers.push_back(l);
            }
            OGRFeature::DestroyFeature(poFeature);
        }
        callback(l);
        GDALClose(poDS);
    }

    /**
     *
     * @param path
     * @param callback1
     * @param callback2
     * @param callback3
     * @param callback4
     * @return
     */
    vector<struct layer> start(config config, string path, std::function<void ((vector<string> fileNames))> callback1,
                               std::function<void ((layer l))> callback2,
                               void (*callback3)(vector<struct layer> layers), void (*callback4)(layer l)) {
        GDALAllRegister();
        vector<string> extensions{{".tab", ".shp", ".gml", ".geojson", ".gpkg", ".gdb", ".fgb"}};
        vector<string> fileNames;
        string file;
        string fileExtension;
        if (path.find(".gdb") != string::npos) {
            fileNames.push_back(path);
        } else {
            try {
                for (auto &p: filesystem::recursive_directory_iterator(path)) {
                    if (!config.nln.empty() && config.import && !config.append) {
                        printf("ERROR: Can't use alternative table name for importing directories. All tables will be named alike.\n");
                        exit(1);
                    }
                    file = p.path().string();
                    fileExtension = p.path().extension().string();
                    if (caseInsCompare(fileExtension, extensions)) {
                        fileNames.push_back(file);
                    }
                }
            } catch (const std::exception &e) {
                if (!filesystem::exists(path)) {
                    printf("ERROR: Could not open directory or file.\n");
                    exit(1);
                };
                fileNames.push_back(path);
            }
        }
        callback1(fileNames);
        for (const string &fileName: fileNames) {
            pool.push_task(openSource, fileName, callback2);
        }
        pool.wait_for_tasks();
        int i{0};
        // Import in PostGIS
        if (config.import) {
            callback3(layers);
            for (const struct layer &l: layers) {
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
    translate(config config, layer l, const string &encoding, int index, bool first, void (*callback)(layer l)) {
        char **argv{nullptr};
        string altName = l.layerName;
        string env = "PGCLIENTENCODING=" + encoding;
        ctx myctx = {
                .layerIndex =  index,
                .error = false,
        };
        CPLPushErrorHandlerEx(&pgErrorHandler, &myctx);
        putenv((char *) env.c_str());
        setvbuf(stdout, nullptr, _IOFBF, BUFSIZ);
        if (!config.nln.empty()) {
            altName = config.nln;
            if (l.layerIndex > 0) {
                altName = altName + "_" + to_string(l.layerIndex);
            }
        }
        altName = config.schema + "." + altName;
        if ((l.type == "point" || l.type == "linestring" || l.type == "polygon") &&
            (l.singleMultiMixed || config.p_multi)) {
            l.type = "multi" + l.type;
        }
        const char *targetSrs = reinterpret_cast<const char *>(l.wktString != "" ? l.wktString.c_str()
                                                                                 : config.s_srs.c_str());
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
        argv = CSLAddString(argv, "-lco");
        argv = CSLAddString(argv, "GEOMETRY_NAME=the_geom");
        argv = CSLAddString(argv, "-lco");
        argv = CSLAddString(argv, "FID=gid");
        argv = CSLAddString(argv, "-lco");
        argv = CSLAddString(argv, "PRECISION=NO");
        argv = CSLAddString(argv, "-nlt");
        argv = CSLAddString(argv, l.type.c_str());
        argv = CSLAddString(argv, "-s_srs"); // source projection
        argv = CSLAddString(argv, targetSrs);
        argv = CSLAddString(argv, "-t_srs");
        argv = CSLAddString(argv,
                            reinterpret_cast<const char *>(strcmp(l.authStr.c_str(), "-") != 0 ? l.authStr.c_str() :
                                                           !config.t_srs.empty() ? config.t_srs.c_str()
                                                                                 : "EPSG:4326")); // Convert to this
        argv = CSLAddString(argv, "-nln");
        argv = CSLAddString(argv, altName.c_str());
        argv = CSLAddString(argv, l.layerName.c_str());

        GDALDatasetH pgDs = GDALOpenEx(config.connection.c_str(), GDAL_OF_UPDATE | GDAL_OF_VECTOR,
                                       nullptr, nullptr, nullptr);
        GDALDatasetH sourceDs = GDALOpenEx(l.file.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);

        int bUsageError{FALSE};
        GDALVectorTranslateOptions *opt = GDALVectorTranslateOptionsNew(argv, nullptr);
        auto *dst = (GDALDataset *) GDALVectorTranslate(nullptr, pgDs, 1, &sourceDs, opt, &bUsageError);
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