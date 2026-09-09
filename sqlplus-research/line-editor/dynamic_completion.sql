-- Research template only. It is not executed by the local experiments.
-- The line editor consumes one tab-separated record per output row. The
-- production example/refresh-objects.sh emits the complete snapshot and
-- replaces it atomically; this file shows the corresponding read-only shape.

-- Session identity used for unqualified-name resolution.
SELECT 'META'||chr(9)||'CURRENT_SCHEMA'||chr(9)||
       sys_context('USERENV', 'CURRENT_SCHEMA')
FROM dual;

-- Object-name completion. ALL_OBJECTS exposes objects visible to the
-- metadata session, which is the desired default for least-surprise results.
SELECT 'OBJECT'||chr(9)||owner||chr(9)||object_name||chr(9)||object_type
FROM   all_objects
WHERE  object_name LIKE :object_prefix ESCAPE '\'
AND    (:owner_prefix IS NULL OR owner LIKE :owner_prefix ESCAPE '\')
ORDER BY owner, object_name, object_type
FETCH FIRST 100 ROWS ONLY;

-- Synonym completion and target resolution. A non-null DB_LINK is retained
-- in the record so the editor can avoid pretending that remote columns are
-- locally available.
SELECT 'SYNONYM'||chr(9)||owner||chr(9)||synonym_name||chr(9)||
       table_owner||chr(9)||table_name||chr(9)||nvl(db_link, '')
FROM   all_synonyms
WHERE  synonym_name LIKE :synonym_prefix ESCAPE '\'
AND    (:owner_prefix IS NULL OR owner LIKE :owner_prefix ESCAPE '\')
ORDER BY owner, synonym_name
FETCH FIRST 100 ROWS ONLY;

-- After the user has typed OWNER.TABLE. (or an unqualified table name after
-- resolution), use the selected owner/table to complete columns.
SELECT 'COLUMN'||chr(9)||owner||chr(9)||table_name||chr(9)||column_name||
       chr(9)||to_char(column_id, 'FM9999999990')
FROM   all_tab_columns
WHERE  owner = :owner
AND    table_name = :table_name
AND    column_name LIKE :column_prefix ESCAPE '\'
ORDER BY column_id
FETCH FIRST 100 ROWS ONLY;
