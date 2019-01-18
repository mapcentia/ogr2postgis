#include "ogrsf_frmts.h"
#include "gdal_utils.h"
#include <iostream>
#include <vector>
#include <list>
#include <experimental/filesystem>
#include <sys/resource.h>


using namespace std;

namespace fs = experimental::filesystem;

/**
 *
 * @param a
 * @param b
 * @return
 */
inline bool caseInsCharCompareN(char a, char b) {
    return (toupper(a) == toupper(b));
}

/**
 *
 * @param s1
 * @param s2
 * @return
 */
bool caseInsCompare(const string &s1, vector<string> s2) {
    for (string text : s2) {
        if ((s1.size() == text.size()) && equal(s1.begin(), s1.end(), text.begin(), caseInsCharCompareN))
            return true;
    }
    return false;
}

/**
 *
 * @param pszFilename
 * @param argv
 * @param encoding
 * @param layerName
 * @param featureCount
 * @return
 */
bool translate(const char *pszFilename, char **argv, char const *encoding, char *layerName, int featureCount) {
    const char *one = "PGCLIENTENCODING=";
    char buf[100];
    strcpy(buf, one);
    strcat(buf, encoding);
    putenv(buf);

    GDALDatasetH TestDs = GDALOpenEx(pszFilename, GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
    GDALDatasetH pgDs = GDALOpenEx("PG:host=127.0.0.1 user=gc2 password=1234 dbname=dk", GDAL_OF_VECTOR,
                                   nullptr, nullptr, nullptr);
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

int main() {

    GDALAllRegister();

    const int maxFeatures = 1;
    const char *dir = "/home/mh/Data/geodk/gml";
    //const char *dir = "/home/mh/Data/geodk/all/DK_SHAPE_UTM32-EUREF89/FOT/DIVERSE";
    string file;
    string fileExtension;
    vector<string> extensions = {".tab", ".shp", ".gml", ".geojson", ".json"};

    for (auto &p: fs::recursive_directory_iterator(dir)) {
        file = p.path().string();
        fileExtension = p.path().extension();

        if (caseInsCompare(fileExtension, extensions)) {

            cout << file.c_str() << " ";

            auto poDS = static_cast<GDALDataset *>(  GDALOpenEx(file.c_str(), GDAL_OF_VECTOR, nullptr, nullptr,
                                                                nullptr));
            if (poDS == nullptr) {
                printf("Open failed.\n");
                exit(1);
            }

            OGRSpatialReference *projection;
            char *wktString;
            const char *authorityName = "";
            const char *authorityCode = "";
            if (poDS->GetLayer(0)->GetSpatialRef() != nullptr) {
                projection = poDS->GetLayer(0)->GetSpatialRef();
                projection->exportToWkt(&wktString);
                authorityName = projection->GetAttrValue("AUTHORITY", 0);
                authorityCode = projection->GetAttrValue("AUTHORITY", 1);
            } else {
                authorityName = "";
                authorityCode = "Na";
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
                    std::cout << wkbFlatten(poGeometry->getGeometryType()) << " ";
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

                std::cout << poDS->GetLayer(0)->GetName() << " Type:" << type << " Layer count: " << featureCount
                          << " Projection: "
                          << authorityName << ":" << authorityCode << " " << " Feature count: " << featureCount << " ";
                std::cout << std::endl;

                OGRFeature::DestroyFeature(poFeature);

                char **argv = nullptr;
                argv = CSLAddString(argv, "-f");
                argv = CSLAddString(argv, "PostgreSQL");
                argv = CSLAddString(argv, "-lco");
                argv = CSLAddString(argv, "GEOMETRY_NAME=the_geom");
                argv = CSLAddString(argv, "-lco");
                argv = CSLAddString(argv, "FID=gi'");
                argv = CSLAddString(argv, "-lco");
                argv = CSLAddString(argv, "PRECISION=NO");
                argv = CSLAddString(argv, "-nlt");
                argv = CSLAddString(argv, type);
                argv = CSLAddString(argv, "-nln");

                char layerName[100];   // array to hold the result.
                strcpy(layerName, "geodk."); // copy string one into the result.
                strcat(layerName, poDS->GetLayer(0)->GetName()); // append string two to the result.

                argv = CSLAddString(argv, layerName);

                if (translate(file.c_str(), argv, "UTF8", layerName, featureCount) == FALSE) {
                    translate(file.c_str(), argv, "LATIN1", layerName, featureCount);
                };

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
}

