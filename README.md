# ogr2postgis
ogr2postgis iterate recursive through a directory tree and prints info about found geo-spatial vector file formats. Optional import files into to a PostGIS database.  

Features:
- Multi-threaded read and import of data - all available CPU cores are used.  

- Each layer of multi-layered files (e.g. GeoPackage and GML) are processes.   

- The first 1.000 features of each layer are read to determine the geometry type. If mixed geometry then "GEOMETRY" is reported. If same geometry but mixed single/multi-part a "(m)" is added to the reported type and features are promoted to multi-part when imported into PostGIS.  

- Layers are attempted to be imported with encoding UTF8. If this fails a retry with a fallback character set is done. Default fallback is LATIN1.   

- Reports projection and authority code for layers. If this information doesn't exist fallback source/target SRS can be set when importing to PostGIS.    

- Error reporting for both read and import.   
  
Will only read files with these extensions (case insensitive) .tab, .shp, .gml, .geojson .json, .gpkg, .gdb, .fgb  
<pre>  
Usage:
Usage: ogr2postgis [options] path 

Positional arguments:
path                    [DIRECTORY|FILE]

Optional arguments:
-h --help               shows help message and exits [default: false]
-v --version            prints version information and exits [default: false]
-o --schema             Output PostgreSQL schema, Defaults to public.
-t --t_srs              Fallback target SRS. Will be used if no authority name/code is available. Defaults to EPSG:4326.
-s --s_srs              Fallback source SRS. Will be used if file doesn't contain projection information.
-n --nln                Alternative table name. Can only be used when importing single file - not directories unless --append is used.
-e --encoding           Fallback encoding. Will be used if UTF8 fails [default: "LATIN1"]
-i --import             Import found files into PostgreSQL/PostGIS [default: false]
-p --p_multi            Promote single geometries to multi part. [default: false]
-a --append             Append to existing layer instead of creating new. [default: false]
-c --connection         PGDATASOURCE postgres datasource. E.g."dbname='databasename' host='addr' port='5432' user='x' password='y'"
</pre>

Example:
<pre>
$ ogr2postgis . 
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
|     Driver     | Count |        Type        | Layer no. |        Name        | Proj |    Auth    |                           File                           |                                  Error                                 |
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
|                | 0     |                    | 0         |                    |      |            | mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/RETSKR.tab     | Open() failed for mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/RETSKR.dat |
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
| GPKG           | 553   | point(m)           | 0         | train_stations     | True | EPSG:25832 | mapinfo/test.gpkg                                        |                                                                        |
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
| ESRI Shapefile | 27    | multilinestring(m) | 0         | latin1             | True | EPSG:25832 | mapinfo/latin1.shp                                       |                                                                        |
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
| GPKG           | 218   | polygon            | 1         | city_center        | True | EPSG:25832 | mapinfo/test.gpkg                                        |                                                                        |
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
| GPKG           | 2     | linestring         | 2         | layers:linje       | True | EPSG:4326  | mapinfo/test.gpkg                                        |                                                                        |
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
| GPKG           | 27    | multilinestring    | 3         | test               | True | EPSG:25832 | mapinfo/test.gpkg                                        |                                                                        |
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
| MapInfo File   | 350   | polygon(m)         | 0         | BDC_Land_Interests | True | -          | mapinfo/BDC_Land_Interests.tab                           |                                                                        |
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
| MapInfo File   | 1330  | polygon            | 0         | POSTNUMMER         | True | -          | mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/POSTNUMMER.tab |                                                                        |
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
| MapInfo File   | 1484  | polygon            | 0         | POLITIKR           | True | -          | mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/POLITIKR.tab   |                                                                        |
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
| MapInfo File   | 1478  | polygon            | 0         | REGION             | True | -          | mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/REGION.tab     |                                                                        |
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
| MapInfo File   | 1575  | polygon            | 0         | KOMMUNE            | True | -          | mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/KOMMUNE.tab    |                                                                        |
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
| MapInfo File   | 1574  | polygon            | 0         | OPSTILKR           | True | -          | mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/OPSTILKR.tab   |                                                                        |
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
| MapInfo File   | 3856  | polygon            | 0         | SOGN               | True | -          | mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/SOGN.tab       |                                                                        |
+----------------+-------+--------------------+-----------+--------------------+------+------------+----------------------------------------------------------+------------------------------------------------------------------------+
Total of 13 layer(s) in 10 file(s) processed in 626ms using GDAL 3.4.1, released 2021/12/27



</pre>
