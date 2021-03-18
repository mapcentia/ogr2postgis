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
