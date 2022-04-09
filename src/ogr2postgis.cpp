/*
 * @author     Martin Høgh <mh@mapcentia.com>
 * @copyright  2013-2022 MapCentia ApS
 * @license    http://www.gnu.org/licenses/#AGPL  GNU AFFERO GENERAL PUBLIC LICENSE 3
 */

#include <getopt.h>
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

using namespace std;
using namespace tabulate;

int main(int argc, char *argv[]) {
    int opt;
    int long_index{0};
    const char *programName{argv[0]};
    const char *path;
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "-?") == 0) {
            help(programName);
            exit(0);
        }
    }
    //Specifying the expected options
    static struct option long_options[] = {
            {"connection", required_argument, nullptr, 'c'},
            {"schema",     required_argument, nullptr, 'o'},
            {"t_srs",      required_argument, nullptr, 't'},
            {"s_srs",      required_argument, nullptr, 's'},
            {"nln",        required_argument, nullptr, 'n'},
            {"import",     no_argument,       nullptr, 'i'},
            {"p_multi",    no_argument,       nullptr, 'm'},
            {"append",     no_argument,       nullptr, 'a'},
            {"help",       no_argument,       nullptr, 'h'},
            {"help",       no_argument,       nullptr, '?'},
            {nullptr, 0,                      nullptr, 0}
    };
    while ((opt = getopt_long(argc, argv, "c:o:t:s:n:i:p:a:", long_options, &long_index)) != -1) {
        switch (opt) {
            case 'c':
                connection = optarg;
                break;
            case 'o':
                schema = optarg;
                break;
            case 't':
                t_srs = optarg;
                break;
            case 's':
                s_srs = optarg;
                break;
            case 'n':
                nln = optarg;
                break;
            case 'i':
                import = true;
                optind--;
                break;
            case 'p':
                p_multi = true;
                optind--;
                break;
            case 'a':
                append = true;
                optind--;
                break;
            default:
                help(programName);
                exit(1);
        }
    }
    if (argc < 2) {
        help(programName);
        exit(1);
    }
    // optind is for the extra arguments
    // which are not parsed
    for (; optind < argc; optind++) {
        path = argv[optind];
    }
    GDALAllRegister();
    start(path);
}

void help(const char *programName) {
    printf("%s iterate recursive through a directory tree and extracts info about detected geo-spatial vector file formats. Optional import files into to a PostGIS database.\n",
           programName);
    printf("If mixed single and multi part geometry are detected then geometry will be promoted to multi when importing.\n");
    printf("If mixed geometry types are detected the type is set to 'geometry' when importing.\n\n");
    printf("Will only detect files with these extensions (case insensitive) .tab, .shp, .gml, .geojson, .gpkg, .gdb\n\n");
    printf("Usage:\n");
    printf("  %s [OPTION]... [DIRECTORY|FILE]\n", programName);

    printf("\nGeneral options:\n");
    printf("  -?, --help                    Show this help, then exit\n");

    printf("\nOptions controlling the import to postgis:\n");
    printf("  -i, --import                  Optional. Import found files into PostgreSQL/PostGIS.\n");
    printf("  -o, --schema                  Optional. Output PostgreSQL schema, Defaults to public.\n");
    printf("  -s, --s_srs                   Optional. Fallback source SRS. Will be used if file doesn't contain projection information.\n");
    printf("  -t, --t_srs                   Optianal. Fallback target SRS. Will be used if no authority name/code is available. Defaults to EPSG:4326.\n");
    printf("  -n, --nln                     Optional. Alternative table name. Can only be used when importing single file - not directories unless --append is used.\n");
    printf("  -p, --p_multi                 Optional. Promote single geometries to multi part.\n");
    printf("  -a, --append                  Optional. Append to existing layer instead of creating new.\n");

    printf("\nConnection options:\n");
    printf("  -c, --connection=PGDATASOURCE postgres datasource. E.g.\"dbname='databasename' host='addr' port='5432' user='x' password='y'\"\n");

//    printf("\nIf no database name is supplied, then the PGDATABASE environment\n"
//           "variable value is used.\n\n");
//    printf("Report bugs to <%s>.\n"), PACKAGE_BUGREPORT;
//    printf("%s home page: <%s>\n"), PACKAGE_NAME, PACKAGE_URL;
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
                if (nln && import && !append) {
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
            if (l.error == "") {
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
        table.add_row({l.driverName.c_str(), to_string(l.featureCount),  l.type + (l.singleMultiMixed ? "(m)": ""), to_string(l.layerIndex), l.layerName,
                       l.hasWkt, l.authStr, l.file, l.error}).format();
        i++;
        if (l.error != "") {
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
    if (l.error != "" || poDS == nullptr) {
        l.error = l.error != "" ? l.error : "Unable to open file";
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
                if (tmpType != "" && (tmpType != type && tmpType != "multi" + type && tmpType != type.substr(5, type.length()))) {
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
    if (nln) {
        altName = nln;
        if (l.layerIndex > 0) {
            altName = altName + "_" + to_string(l.layerIndex);
        }
    }
    altName = schema + "." + altName;
    if ((l.type == "point" || l.type == "linestring" || l.type == "polygon") && (l.singleMultiMixed || p_multi)) {
        l.type = "multi" + l.type;
    }
    const char *targetSrs = reinterpret_cast<const char *>(l.wktString != nullptr ? l.wktString : s_srs);
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
                                                       t_srs != nullptr ? t_srs
                                                                        : "EPSG:4326")); // Convert to this
    argv = CSLAddString(argv, "-nln");
    argv = CSLAddString(argv, altName.c_str());
    argv = CSLAddString(argv, l.layerName.c_str());

    GDALDatasetH pgDs = GDALOpenEx(connection, GDAL_OF_UPDATE | GDAL_OF_VECTOR,
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
