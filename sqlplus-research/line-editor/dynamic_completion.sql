-- Research template only. It is not executed by the local experiments.
-- A front end should bind values and escape %, _, and \ in :prefix.

-- Object-name completion. ALL_OBJECTS exposes only objects visible to the
-- metadata session, which is the desired default for least-surprise results.
SELECT owner, object_name, object_type
FROM   all_objects
WHERE  object_name LIKE :object_prefix ESCAPE '\'
AND    (:owner_prefix IS NULL OR owner LIKE :owner_prefix ESCAPE '\')
ORDER BY owner, object_name, object_type
FETCH FIRST 100 ROWS ONLY;

-- Synonym completion can add names that are valid in the current session.
SELECT owner, synonym_name, table_owner, table_name
FROM   all_synonyms
WHERE  synonym_name LIKE :synonym_prefix ESCAPE '\'
AND    (:owner_prefix IS NULL OR owner LIKE :owner_prefix ESCAPE '\')
ORDER BY owner, synonym_name
FETCH FIRST 100 ROWS ONLY;

-- After the user has typed OWNER.TABLE. (or an unqualified table name after
-- resolution), use the selected owner/table to complete columns.
SELECT owner, table_name, column_name, column_id
FROM   all_tab_columns
WHERE  owner = :owner
AND    table_name = :table_name
AND    column_name LIKE :column_prefix ESCAPE '\'
ORDER BY column_id
FETCH FIRST 100 ROWS ONLY;
