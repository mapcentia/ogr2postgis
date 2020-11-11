#include "ogrsf_frmts.h"
#include "gdal_utils.h"
#include <getopt.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <list>
#include <experimental/filesystem>
#include <sys/resource.h>

using namespace std;

namespace fs = experimental::filesystem;


inline bool caseInsCharCompareN(char a, char b);

bool caseInsCompare(const string &s1, vector<string> s2);

bool translate(const char *pszFilename, char **argv, char const *encoding, char *layerName, int featureCount);

void start(char const *path);

static void help(const char *progname);

int main(int argc, char *argv[]) {
    int opt;
    int long_index = 0;

    const char *progname;
    const char *host;
    const char *port;
    const char *user;
    const char *dbname;
    const char *path;


    progname = argv[0];
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0) {
            help(progname);
            exit(0);
        }
    }

    //Specifying the expected options
    //The two options l and b expect numbers as argument
    static struct option long_options[] = {
            {"host",   required_argument, nullptr, 'h'},
            {"post",   required_argument, nullptr, 'p'},
            {"user",   required_argument, nullptr, 'u'},
            {"dbname", required_argument, nullptr, 'd'},
            {"help",   no_argument,       nullptr, '?'},
            {nullptr, 0,                  nullptr, 0}
    };
    while ((opt = getopt_long(argc, argv, "h:p:U:d:", long_options, &long_index)) != -1) {
        switch (opt) {
            case 'h':
                printf("host: %s\n", optarg);
                host = optarg;
                break;
            case 'p':
                printf("port: %s\n", optarg);
                port = optarg;
                break;
            case 'U':
                printf("user: %s\n", optarg);
                user = optarg;
                break;
            case 'd':
                printf("dbname: %s\n", optarg);
                dbname = optarg;
                break;
            default:
                help(progname);
                exit(1);
        }
    }

    // optind is for the extra arguments
    // which are not parsed
    for (; optind < argc; optind++) {
        path = argv[optind];
    }

    GDALAllRegister();
    start(path);
}


void help(const char *progname) {
    printf("%s dumps a database as a text file or to other formats.\n\n", progname);
    printf("Usage:\n");
    printf("  %s [OPTION]... [DBNAME]\n", progname);

    printf("\nGeneral options:\n");
    printf("  -?, --help                   show this help, then exit\n");

    printf("\nOptions controlling the output content:\n");
    printf("  -a, --data-only              dump only the data, not the schema\n");

    printf("\nConnection options:\n");
    printf("  -d, --dbname=DBNAME      database to dump\n");
    printf("  -h, --host=HOSTNAME      database server host or socket directory\n");
    printf("  -p, --port=PORT          database server port number\n");
    printf("  -U, --username=NAME      connect as specified database user\n");
    printf("  -w, --no-password        never prompt for password\n");
    printf("  -W, --password           force password prompt (should happen automatically)\n");

    printf("\nIf no database name is supplied, then the PGDATABASE environment\n"
           "variable value is used.\n\n");
//    printf("Report bugs to <%s>.\n"), PACKAGE_BUGREPORT;
//    printf("%s home page: <%s>\n"), PACKAGE_NAME, PACKAGE_URL;
}

void start(const char *path) {
    const int maxFeatures{1};
    string file;
    string fileExtension;
    int countf = 0;
    vector<string> extensions = {".tab", ".shp", ".gml", ".geojson", ".json"};

    for (auto &p: fs::recursive_directory_iterator(path)) {
        file = p.path().string();
        fileExtension = p.path().extension();

        if (caseInsCompare(fileExtension, extensions)) {
            countf++;
            auto poDS = static_cast<GDALDataset *>(  GDALOpenEx(file.c_str(), GDAL_OF_VECTOR, nullptr, nullptr,
                                                                nullptr));
            if (poDS == nullptr) {
                printf("Open failed.\n");
                exit(1);
            }

            OGRSpatialReference *projection;
            char *wktString;
            const char *authorityName;
            const char *authorityCode;
            const char *hasWkt = "True";
            const char *driverName = poDS->GetDriverName();
            char authStr[100];
            const OGRSpatialReference *reference = poDS->GetLayer(0)->GetSpatialRef();
            if (reference != nullptr) {
                projection = poDS->GetLayer(0)->GetLayerDefn()->OGRFeatureDefn::GetGeomFieldDefn(0)->GetSpatialRef();
                projection->exportToWkt(&wktString);
                authorityName = projection->GetAuthorityName(nullptr);
                authorityCode = projection->GetAuthorityCode(nullptr);
                if (authorityName != nullptr && authorityCode != nullptr) {
                    const char *sep = ":";
                    strcpy(authStr, authorityName);
                    strcat(authStr, sep);
                    strcat(authStr, authorityCode);
                } else {

                    strcpy(authStr, "-");
                }
            } else {
                authorityName = "";
                authorityCode = "Na";
                hasWkt = "False";
                strcpy(authStr, "-");
            }

            int layerCount = poDS->GetLayerCount();

            // Get the first OGRLayer
            OGRLayer *poLayer = poDS->GetLayerByName(poDS->GetLayer(0)->GetName());

            // Count features
            GIntBig featureCount = poLayer->GetFeatureCount();

            int count = 0;
            OGRFeature *poFeature;
            while ((poFeature = poLayer->GetNextFeature()) != nullptr) {
                OGRGeometry *poGeometry = poFeature->GetGeometryRef();
                const char *type;
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

                printf("%*s %*llu %*s %*i %*s %*s %*s %s", -14, driverName, 8, featureCount, -14, type,  3, layerCount, -30, poDS->GetLayer(0)->GetName(), 10, hasWkt, -12, authStr, file.c_str());

                OGRFeature::DestroyFeature(poFeature);

                char **argv = nullptr;
                argv = CSLAddString(argv, "-f");
                argv = CSLAddString(argv, "PostgreSQL");
                argv = CSLAddString(argv, "-lco");
                argv = CSLAddString(argv, "GEOMETRY_NAME=the_geom");
                argv = CSLAddString(argv, "-lco");
                argv = CSLAddString(argv, "FID=gid");
                argv = CSLAddString(argv, "-lco");
                argv = CSLAddString(argv, "PRECISION=NO");
                argv = CSLAddString(argv, "-nlt");
                argv = CSLAddString(argv, type);
                argv = CSLAddString(argv, "-nln");

                char layerName[100];   // array to hold the result.
                strcpy(layerName, "geodk."); // copy string one into the result.
                strcat(layerName, poDS->GetLayer(0)->GetName()); // append string two to the result.

                argv = CSLAddString(argv, layerName);

                //if (translate(file.c_str(), argv, "UTF8", layerName, featureCount) == FALSE) {
                //    translate(file.c_str(), argv, "LATIN1", layerName, featureCount);
                //};

                count++;

                if (count == maxFeatures || count == featureCount) {
                    std::cout << std::endl;
                    CSLDestroy(argv);
                    break;
                } else {
                    continue;
                }
            }
            GDALClose(poDS);
        }
    }
    printf("Total %i\n", countf);
}

bool caseInsCompare(const string &s1, vector<string> s2) {
    for (string text : s2) {
        if ((s1.size() == text.size()) && equal(s1.begin(), s1.end(), text.begin(), caseInsCharCompareN))
            return true;
    }
    return false;
}

bool caseInsCharCompareN(char a, char b) {
    return (toupper(a) == toupper(b));
}

bool translate(const char *pszFilename, char **argv, const char *encoding, char *layerName, int featureCount) {
    const char *one{"PGCLIENTENCODING="};
    char buf[100];
    strcpy(buf, one);
    strcat(buf, encoding);
    putenv(buf);

    GDALDatasetH TestDs = GDALOpenEx(pszFilename, GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
    GDALDatasetH pgDs = GDALOpenEx("PG:host=127.0.0.1 user=gc2 password=1234 dbname=dk", GDAL_OF_VECTOR,
                                   nullptr, nullptr, nullptr);
    if (pgDs == nullptr) {
        exit(1);
    }

    int bUsageError = FALSE;
    GDALVectorTranslateOptions *opt = GDALVectorTranslateOptionsNew(argv, nullptr);
    auto *dst = (GDALDataset *) GDALVectorTranslate(nullptr, pgDs, 1, &TestDs, opt, &bUsageError);

    if (dst == nullptr) {
        std::cout << "ERROR!" << std::endl;
        GDALVectorTranslateOptionsFree(opt);
        GDALClose(dst);
        return TRUE;
    } else {
        OGRLayer *layer = dst->GetLayerByName(layerName);

        if (layer->GetFeatureCount() != featureCount) {
            std::cout << "Try again" << std::endl;
            GDALVectorTranslateOptionsFree(opt);
            GDALClose(dst);
            return FALSE;
        }
        std::cout << "Layer translated" << std::endl;
        GDALVectorTranslateOptionsFree(opt);
        GDALClose(dst);
        return TRUE;
    }
}
