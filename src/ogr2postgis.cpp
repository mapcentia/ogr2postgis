/*
 * @author     Martin Høgh <mh@mapcentia.com>
 * @copyright  2013-2023 MapCentia ApS
 * @license    http://www.gnu.org/licenses/#AGPL  GNU AFFERO GENERAL PUBLIC LICENSE 3
 */

#include "ogr2postgis.hpp"
#include "argparse.hpp"
#include "indicators.hpp"
#include "tabulate.hpp"
#include <chrono>


using namespace argparse;
using namespace ogr2postgis;
using namespace tabulate;
using namespace indicators;


ProgressBar readBar{
        option::BarWidth{50},
        option::ForegroundColor{indicators::Color::white},
        option::FontStyles{
                std::vector<indicators::FontStyle>{indicators::FontStyle::bold}
        },
        option::PostfixText{"Analyzing files"},
};
ProgressBar importBar{
        option::BarWidth{50},
        option::ForegroundColor{indicators::Color::white},
        option::FontStyles{
                std::vector<indicators::FontStyle>{indicators::FontStyle::bold}
        },
        option::PostfixText{"Importing to PostgreSQL"},
};

int main(int argc, char *argv[]) {
    ArgumentParser program("ogr2postgis", "2022.5.0");
    program.add_argument("-o", "--schema").help("Output PostgreSQL schema.").default_value(
            std::string{"public"});
    program.add_argument("-t", "--t_srs").help(
            "Fallback target SRS. Will be used if no authority name/code is available.").default_value(
            std::string{"EPSG:4326"});
    program.add_argument("-s", "--s_srs").help(
            "Fallback source SRS. Will be used if file doesn't contain projection information.");
    program.add_argument("-n", "--nln").help(
            "Alternative table name. Can only be used when importing single file - not directories unless --append is used.");
    program.add_argument("-e", "--encoding").help("Fallback encoding. Will be used if UTF8 fails").default_value(
            std::string{"LATIN1"});
    program.add_argument("-i", "--import").help("Import found files into PostgreSQL/PostGIS").default_value(
            false).implicit_value(true);
    program.add_argument("-p", "--p_multi").help("Promote single geometries to multi part.").default_value(
            false).implicit_value(true);
    program.add_argument("-a", "--append").help("Append to existing layer instead of creating new.").default_value(
            false).implicit_value(true);
    program.add_argument("-c", "--connection").help(
            "PGDATASOURCE postgres datasource. E.g.\"PG:host='addr' dbname='databasename' port='5432' user='x' password='y'\"");
    program.add_argument("path").help("[DIRECTORY|FILE]");
//    program.add_epilog("Possible things include betingalw, chiz, and res.");

    try {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }

    config config;

    // Without defaults
    if (program.present("--connection")) {
        config.connection = program.get("c");
    }
    if (program.present("--s_srs")) {
        config.s_srs = program.get("s");
    }
    if (program.present("--nln")) {
        config.nln = program.get("n");
    }
    // With defaults
    config.schema = program.get<std::string>("--schema");
    config.fallbackEncoding = program.get<std::string>("--encoding");
    config.t_srs = program.get<std::string>("--t_srs");
    config.import = program.get<bool>("--import");
    config.append = program.get<bool>("--append");
    config.p_multi = program.get<bool>("--p_multi");

    auto path = program.get("path");

    auto lCallback1 = [](std::vector<std::string> fileNames) {
        readBar.set_option(indicators::option::MaxProgress{fileNames.size()});
    };

    auto lCallback2 = [](layer l) {
        readBar.tick();
    };

    auto lCallback3 = [](std::vector<struct layer> layers) {
        importBar.set_option(indicators::option::MaxProgress{layers.size()});
        std::cout << "Callback 3 called" << std::endl;
    };

    auto lCallback4 = [](layer l) {
        importBar.tick();
    };

    std::vector<struct layer> layers = start(config, path, lCallback1, lCallback2, lCallback3, lCallback4);

    // Print out
    Table table;
    auto startTime = std::chrono::high_resolution_clock::now();
    int i{0};
    table.add_row({"Driver", "Count", "Type", "Layer no.", "Name", "Proj", "Auth", "File", "Error"});
    table[0].format()
            .font_align(FontAlign::center)
            .font_style({tabulate::FontStyle::underline, tabulate::FontStyle::bold});
    i = 0;
    for (const struct layer &l: layers) {
        table.add_row({l.driverName.c_str(), std::to_string(l.featureCount), l.type + (l.singleMultiMixed ? "(m)" : ""),
                       std::to_string(l.layerIndex), l.layerName,
                       l.hasWkt, l.authStr, l.file, l.error}).format();
        i++;
        if (!l.error.empty()) {
            table[i][8].format().font_color(tabulate::Color::red);
        }

    }
    std::cout << "\r" << std::flush;
    std::cout << table << std::endl;
    auto stopTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stopTime - startTime);
//    printf("Total of %zu layer(s) in %zu file(s) processed in %ldms using %s\n", layers.size(), fileNames.size(),
//           lround(duration.count()/1000), GDALVersionInfo("--version"));
}



