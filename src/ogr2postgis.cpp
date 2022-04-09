/*
 * @author     Martin Høgh <mh@mapcentia.com>
 * @copyright  2013-2022 MapCentia ApS
 * @license    http://www.gnu.org/licenses/#AGPL  GNU AFFERO GENERAL PUBLIC LICENSE 3
 */

#include <iostream>
#include <vector>
#include <list>
#include <experimental/filesystem>
#include <chrono>
#include "ogrsf_frmts.h"
#include "gdal_utils.h"
#include "thread_pool.hpp"
#include "indicators.hpp"
#include "tabulate.hpp"
#include "ogr2postgis.hpp"
#include "argparse.hpp"

using namespace std;
using namespace tabulate;

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program("ogr2postgis");
    program.add_argument("-c", "--connection").help("Hej");
    program.add_argument("-o", "--schema");
    program.add_argument("-t", "--t_srs");
    program.add_argument("-s", "--s_srs");
    program.add_argument("-n", "--nln");
    program.add_argument("-i", "--import").help("Hej").default_value(false).implicit_value(true);
    program.add_argument("-p", "--p_multi").default_value(false).implicit_value(true);
    program.add_argument("-a", "--append").default_value(false).implicit_value(true);
    program.add_argument("path")
            .help("display the square of a given number");

    try {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }
    if (program.present("--connection")) {
        connection = program.get("c");
    }
    if (program.present("--schema")) {
        schema = program.get("o");
    } else {
        schema = "public";
    }
    if (program["--import"] == true) {
        import = true;
    };
    if (program.present("--t_srs")) {
        t_srs = program.get("t");
    }
    if (program.present("--s_srs")) {
        s_srs = program.get("s");
    }
    if (program.present("--nln")) {
        nln = program.get("n");
    }
    if (program["--append"] == true) {
        append = true;
    };
    if (program["--p_multi"] == true) {
        p_multi = true;
    };

    auto path = program.get("path");


    GDALAllRegister();
    start(path);
}

void start(string path) {
    vector<string> extensions{{".tab", ".shp", ".gml", ".geojson", ".gpkg", ".gdb"}};
    vector<string> fileNames;
    string file;
    string fileExtension;
    if (path.find(".gdb") != string::npos) {
        fileNames.push_back(path);
    } else {
        try {
            for (auto &p: experimental::filesystem::recursive_directory_iterator(path)) {
                if (!nln.empty() && import && !append) {
                    printf("ERROR: Can't use alternative table name for importing directories. All tables will be named alike.\n");
                    exit(1);
                }
                file = p.path().string();
                fileExtension = p.path().extension();
                if (caseInsCompare(fileExtension, extensions)) {
                    fileNames.push_back(file);
                }
            }
        } catch (const std::exception &e) {
            if (!std::experimental::filesystem::exists(path)) {
                printf("ERROR: Could not open directory or file.\n");
                exit(1);
            };
            fileNames.push_back(path);
        }
    }
    readBar.set_option(indicators::option::MaxProgress{fileNames.size()});
    for (const string &fileName: fileNames) {
        pool.push_task(open, fileName);
    }
    pool.wait_for_tasks();
    std::cout << "\r" << std::flush;
    int i{0};
    // Import in PostGIS
    if (import) {
        importBar.set_option(indicators::option::MaxProgress{layers.size()});
        for (const struct layer &l: layers) {
            if (l.error.empty()) {
                pool.push_task(translate, l, "UTF8", i, true);
            } else {
                importBar.tick();
            }
            i++;
        };
        pool.wait_for_tasks();
    }
    // Print out
    Table table;
    table.add_row({"Driver", "Count", "Type", "Layer no.", "Name", "Proj", "Auth", "File", "Error"});
    table[0].format()
            .font_align(FontAlign::center)
            .font_style({FontStyle::underline, FontStyle::bold});
    i = 0;
    for (const struct layer &l: layers) {
        table.add_row({l.driverName.c_str(), to_string(l.featureCount), l.type + (l.singleMultiMixed ? "(m)" : ""),
                       to_string(l.layerIndex), l.layerName,
                       l.hasWkt, l.authStr, l.file, l.error}).format();
        i++;
        if (!l.error.empty()) {
            table[i][8].format().font_color(Color::red);
        }

    }
    std::cout << "\r" << std::flush;
    std::cout << table << std::endl;
    auto stopTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stopTime - startTime);
    printf("Total of %zu layer(s) in %zu file(s) processed in %zums using %s\n", layers.size(), fileNames.size(),
           duration.count(), GDALVersionInfo("--version"));
}

void open(string file) {
    layer l = {"", 0, "", "", "", file, nullptr,
               "", 0, "", false};
    CPLPushErrorHandlerEx(&openErrorHandler, &l);
    auto *poDS = (GDALDataset *) GDALOpenEx(file.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
    if (!l.error.empty() || poDS == nullptr) {
        l.error = !l.error.empty() ? l.error : "Unable to open file";
        layers.push_back(l);
        readBar.tick();
        return;
    }
    OGRSpatialReference *projection;
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
        string tmpType;
        bool singleMultiMixed{false};
        OGRFeature *poFeature;
        while ((poFeature = layer->GetNextFeature()) != nullptr) {
            OGRGeometry *poGeometry = poFeature->GetGeometryRef();
            if (poGeometry != nullptr) {
                switch (wkbFlatten(poGeometry->getGeometryType())) {
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
            }
            count++;
            if (count == maxFeatures || count == featureCount) {
                break;
            } else {
                if (!tmpType.empty() &&
                    (tmpType != type && tmpType != "multi" + type && tmpType != type.substr(5, type.length()))) {
                    type = "geometry";
                    break;
                }
                if (tmpType == "multi" + type || tmpType == type.substr(5, type.length())) {
                    singleMultiMixed = true;
                }
                tmpType = type;
                continue;
            }
        }
        l = {driverName, featureCount, type, poDS->GetLayer(i)->GetName(), hasWkt, file, wktString,
             authStr, i, "", singleMultiMixed};
        layers.push_back(l);
        OGRFeature::DestroyFeature(poFeature);
    }
    readBar.tick();
    GDALClose(poDS);
}

void
translate(layer l, const string &encoding, int index, bool first) {
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
    if (!nln.empty()) {
        altName = nln;
        if (l.layerIndex > 0) {
            altName = altName + "_" + to_string(l.layerIndex);
        }
    }
    altName = schema + "." + altName;
    if ((l.type == "point" || l.type == "linestring" || l.type == "polygon") && (l.singleMultiMixed || p_multi)) {
        l.type = "multi" + l.type;
    }
    const char *targetSrs = reinterpret_cast<const char *>(l.wktString != nullptr ? l.wktString : s_srs.c_str());
    if (targetSrs == nullptr) {
        layers[index].error = "Can't impoort without source srs";
        CSLDestroy(argv);
        importBar.tick();
        return;
    }
    argv = CSLAddString(argv, "-f");
    argv = CSLAddString(argv, "PostgreSQL");
    if (append) {
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
                                                       !t_srs.empty() ? t_srs.c_str()
                                                                        : "EPSG:4326")); // Convert to this
    argv = CSLAddString(argv, "-nln");
    argv = CSLAddString(argv, altName.c_str());
    argv = CSLAddString(argv, l.layerName.c_str());

    GDALDatasetH pgDs = GDALOpenEx(connection.c_str(), GDAL_OF_UPDATE | GDAL_OF_VECTOR,
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
        translate(l, "LATIN1", index, false);
        return;
    }
    importBar.tick();
}
