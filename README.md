# ogr2postgis
ogr2postgis iterate recursive through a directory tree and prints info about found geo-spatial vector file formats. Optional import files into to a PostGIS database.  
  
Will only read files with these extensions (case insensitive) .tab, .shp, .gml, .geojson .json, .gpkg, .gdb  
<pre>  
Usage:
ogr2postgis [OPTION]... [DIRECTORY|FILE]

General options:  
-?, --helpShow this help, then exit

Options controlling the import to postgis:
-i, --import                  Optional. Import found files into PostgreSQL/PostGIS.  
-o, --schema                  Optional. Output PostgreSQL schema, Defaults to public.  
-s, --s_srs                   Optional. Fallback source SRS. Will be used if file doesn't contain projection information.  
-t, --t_srs                   Optianal. Fallback target SRS. Will be used if no authority name/code is available. Defaults to EPSG:4326.  
-n, --nln                     Optional. Alternative table name. Can only be used when importing single file - not directories unless --append is used.  
-p, --p_multi                 Optional. Promote single geometries to multi part.  
-a, --append                  Optional. Append to existing layer instead of creating new.  
  
Connection options:  
-c, --connection=PGDATASOURCE postgres datasource. E.g."dbname='databasename' host='addr' port='5432' user='x' password='y'"
</pre>

Example:
<pre>
$ ogr2postgis . 
Driver            Count Type      Layer no. Name                                 Proj Auth         File
GML               11029 linestring        1 JERNBANE                             True EPSG:25832   /home/mh/Data/Railroad.gml
GML                 217 polygon           1 BYKERNE                              True EPSG:25832   /home/mh/Data/City_center.gml
OpenFileGDB          99 point             1 baal_vand_p_fuglet_madpakkehus       True EPSG:25832   /home/mh/Data/FileGD/_ags_data218A3C11A8AB47BA9F27F15E186EE457.gdb
ESRI Shapefile      255 multipolygon      1 ne_10m_admin_0_countries             True EPSG:4326    /home/mh/Data/ne_10m_admin_0_countries/ne_10m_admin_0_countries.shp
GPKG                  4 point             1 madpakkehus                          True EPSG:25832   /home/mh/Data/madpakkehus.gpkg
ESRI Shapefile      173 polygon           1 KOMMUNER                            False -            /home/mh/Data/DAGI2M_SHAPE_UTM32-EUREF89/ADM/KOMMUNER.SHP
GML                6523 point             1 VINDMOELLE                           True EPSG:25832   /home/mh/Data/Windmill.gml
GPKG                553 point             1 train_stations                       True EPSG:25832   /home/mh/Data/test.gpkg
                    218 polygon           2 city_center                          True EPSG:25832   
                      2 linestring        3 layers:linje                         True EPSG:4326    
ESRI Shapefile   127218 polygon           1 NAVNE_A                              True -            /home/mh/Data/NAVNE/NAVNE_A.shp
ESRI Shapefile     9573 point             1 NAVNE_P                              True -            /home/mh/Data/NAVNE/NAVNE_P.shp
ESRI Shapefile     3225 multilinestring   1 NAVNE_L                              True -            /home/mh/Data/NAVNE/NAVNE_L.shp
MapInfo File        350 polygon           1 BDC_Land_Interests                   True -            /home/mh/Data/mapinfo/BDC_Land_Interests.tab
MapInfo File       1478 polygon           1 REGION                               True -            /home/mh/Data/mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/REGION.tab
MapInfo File       1575 polygon           1 KOMMUNE                              True -            /home/mh/Data/mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/KOMMUNE.tab
MapInfo File       1330 polygon           1 POSTNUMMER                           True -            /home/mh/Data/mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/POSTNUMMER.tab
MapInfo File       1484 polygon           1 POLITIKR                             True -            /home/mh/Data/mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/POLITIKR.tab
MapInfo File       3856 polygon           1 SOGN                                 True -            /home/mh/Data/mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/SOGN.tab
MapInfo File       1501 polygon           1 RETSKR                               True -            /home/mh/Data/mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/RETSKR.tab
MapInfo File       1574 polygon           1 OPSTILKR                             True -            /home/mh/Data/mapinfo/DAGIREF_MAPINFO_UTM32-EUREF89/ADM/OPSTILKR.tab
GML              140016 polygon           1 StedNavn                             True EPSG:25832   /home/mh/Data/KORT10/KORT10.gml
GML                 518 point             1 TOGSTATION                           True EPSG:25832   /home/mh/Data/Train_station.gml
Total 23
GDAL 3.2.0, released 2020/10/26

</pre>
