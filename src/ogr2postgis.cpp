/*
 * @author     Martin Høgh <mh@mapcentia.com>
 * @copyright  2013-2024 MapCentia ApS
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
        std::vector{indicators::FontStyle::bold}
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
    program.add_argument("-j", "--json").help("Out JSON instead of ascii tables. Useful if output should be processed.")
            .default_value(
                false).implicit_value(true);
    program.add_argument("-d", "--autodetect").help("Auto detect types in CSV files.").default_value(false).
            implicit_value(true);
    program.add_argument("-c", "--connection").help(
        "PGDATASOURCE postgres datasource. E.g.\"PG:host='addr' dbname='databasename' port='5432' user='x' password='y'\"");
    program.add_argument("path").help("[DIRECTORY|FILE]");
    std::string v = GDALVersionInfo("--version");
    program.add_epilog("Build with " + v);

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }

    config config;

    // Without defaults
    if (program.present("--connection")) {
        config.connection = program.get("c");
    }
    config.s_srs = program.present("--s_srs") ? program.get("s") : "";
    config.nln = program.present("--nln") ? program.get("n") : "";
    // With defaults
    config.schema = program.get<std::string>("--schema");
    config.fallbackEncoding = program.get<std::string>("--encoding");
    config.t_srs = program.get<std::string>("--t_srs");
    config.import = program.get<bool>("--import");
    config.append = program.get<bool>("--append");
    config.p_multi = program.get<bool>("--p_multi");
    config.json = program.get<bool>("--json");
    config.autodetect = program.get<bool>("--autodetect");

    auto path = program.get("path");

    auto lCallback1 = [config](const std::vector<std::string> &fileNames) {
        if (!config.json) readBar.set_option(indicators::option::MaxProgress{fileNames.size()});
    };

    auto lCallback2 = [config](const layer &l) {
        if (!config.json) readBar.tick();
    };

    auto lCallback3 = [config](const std::vector<struct layer> &layers) {
        if (!config.json) importBar.set_option(indicators::option::MaxProgress{layers.size()});
    };

    auto lCallback4 = [config](const layer &l) {
        if (!config.json) importBar.tick();
    };

    std::vector<layer> layers = start(config, path, lCallback1, lCallback2, lCallback3, lCallback4);

    // Print out
    if (!config.json) {
        Table table;
        auto startTime = std::chrono::high_resolution_clock::now();
        int i;
        table.add_row({"Driver", "Count", "Type", "Layer no.", "Name", "Proj", "Auth", "File", "Error"});
        table[0].format()
                .font_align(FontAlign::center)
                .font_style({tabulate::FontStyle::underline, tabulate::FontStyle::bold});
        i = 0;
        for (const layer &l: layers) {
            table.add_row({
                l.driverName.c_str(), std::to_string(l.featureCount), l.type + (l.singleMultiMixed ? "(m)" : ""),
                std::to_string(l.layerIndex), l.layerName,
                l.hasWkt, l.authStr, l.file, l.error
            }).format();
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
    } else {
        std::cout << "[" << std::flush;
        for (int i = 0; i < layers.size(); i++) {
            const layer &l = layers[i];
            std::cout << "{" << std::flush;
            std::cout << R"("driver":")" + l.driverName + "\"," << std::flush;
            std::cout << R"("featureCount":)" + std::to_string(l.featureCount) + "," << std::flush;
            std::cout << R"("type":")" + l.type + (l.singleMultiMixed ? "(m)" : "") + "\"," << std::flush;
            std::cout << R"("layerIndex":)" + std::to_string(l.layerIndex) + "," << std::flush;
            std::cout << R"("layerName":")" + l.layerName + "\"," << std::flush;
            std::cout << R"("hasWkt":)" + static_cast<std::string>(l.hasWkt == "True" ? "true" : "false") + "," <<
                    std::flush;
            std::cout << R"("authStr":")" + l.authStr + "\"," << std::flush;
            std::cout << R"("file":")" + l.file + "\"," << std::flush;
            std::cout << R"("error":)" + (!l.error.empty() ? "\"" + l.error + "\"" : "null") + "" << std::flush;
            std::cout << "}" << std::flush;
            if (i < layers.size() - 1) {
                std::cout << "," << std::flush;
            }
        }
        std::cout << "]" << std::flush;
    }
}



