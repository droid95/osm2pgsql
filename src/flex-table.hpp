#ifndef OSM2PGSQL_FLEX_TABLE_HPP
#define OSM2PGSQL_FLEX_TABLE_HPP

#include "db-copy-mgr.hpp"
#include "flex-table-column.hpp"
#include "pgsql.hpp"

#include <osmium/osm/item_type.hpp>

#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
 * An output table (in the SQL sense) for the flex backend.
 */
class flex_table_t
{
public:
    flex_table_t(std::string const &name, int srid,
                 std::shared_ptr<db_copy_thread_t> const &copy_thread,
                 bool append)
    : m_name(name), m_srid(srid), m_copy_mgr(copy_thread), m_append(append)
    {}

    std::string const &name() const noexcept { return m_name; }

    std::string const &schema() const noexcept { return m_schema; }

    std::string const &data_tablespace() const noexcept
    {
        return m_data_tablespace;
    }

    std::string const &index_tablespace() const noexcept
    {
        return m_index_tablespace;
    }

    void set_schema(std::string const &schema) noexcept { m_schema = schema; }

    void set_data_tablespace(std::string const &tablespace) noexcept
    {
        m_data_tablespace = tablespace;
    }

    void set_index_tablespace(std::string const &tablespace) noexcept
    {
        m_index_tablespace = tablespace;
    }

    osmium::item_type id_type() const noexcept { return m_id_type; }

    void set_id_type(osmium::item_type type) noexcept { m_id_type = type; }

    bool has_id_column() const noexcept
    {
        if (m_columns.empty()) {
            return false;
        }
        return (m_columns[0].type() == table_column_type::id_type) ||
               (m_columns[0].type() == table_column_type::id_num);
    }

    std::size_t num_columns() const noexcept { return m_columns.size(); }

    std::vector<flex_table_column_t>::const_iterator begin() const noexcept
    {
        return m_columns.begin();
    }

    std::vector<flex_table_column_t>::const_iterator end() const noexcept
    {
        return m_columns.end();
    }

    bool has_geom_column() const noexcept
    {
        return m_geom_column != std::numeric_limits<std::size_t>::max();
    }

    // XXX should we allow several geometry columns?
    flex_table_column_t const &geom_column() const noexcept
    {
        assert(has_geom_column());
        return m_columns[m_geom_column];
    }

    int srid() const noexcept { return m_srid; }

    std::string build_sql_prepare_get_wkb() const;

    std::string build_sql_create_table(bool final_table) const;

    std::string build_sql_column_list() const;

    /// Does this table take objects of the specified type?
    bool matches_type(osmium::item_type type) const noexcept
    {
        if (m_id_type == osmium::item_type::undefined) {
            return true;
        }
        if (type == m_id_type) {
            return true;
        }
        return m_id_type == osmium::item_type::area &&
               type != osmium::item_type::node;
    }

    /// Map way/node/relation ID to id value used in database table column
    osmid_t map_id(osmium::item_type type, osmid_t id) const noexcept
    {
        if (m_id_type == osmium::item_type::area &&
            type == osmium::item_type::relation) {
            return -id;
        }
        return id;
    }

    flex_table_column_t &add_column(std::string const &name,
                                    std::string const &type);

    void init()
    {
        auto const columns = build_sql_column_list();
        auto const id_columns = id_column_names();
        m_target = std::make_shared<db_target_descr_t>(
            name().c_str(), id_columns.c_str(), columns.c_str());
    }

    void connect(std::string const &conninfo);

    void commit() { m_copy_mgr.sync(); }

    void new_line() { m_copy_mgr.new_line(m_target); }

    void teardown() { m_db_connection.reset(); }

    void prepare()
    {
        assert(m_db_connection);
        if (has_id_column()) {
            m_db_connection->exec(build_sql_prepare_get_wkb());
        }
    }

    void start(std::string const &conninfo);

    void stop(bool updateable);

    void create_id_index();

    void delete_rows_with(osmium::item_type type, osmid_t id);

    pg_result_t get_geom_by_id(osmium::item_type type, osmid_t id) const;

    db_copy_mgr_t<db_deleter_by_type_and_id_t> *copy_mgr() noexcept
    {
        return &m_copy_mgr;
    }

private:
    bool has_multicolumn_id_index() const noexcept;
    std::string id_column_names() const;
    std::string full_name() const;
    std::string full_tmp_name() const;

    /// The name of the table
    std::string m_name;

    /// The schema this table is in
    std::string m_schema{"public"};

    /// The table space used for this table (empty for default tablespace)
    std::string m_data_tablespace;

    /**
     * The table space used for indexes on this table (empty for default
     * tablespace)
     */
    std::string m_index_tablespace;

    /**
     * The columns in this table (The first zero, one or two columns are always
     * the id columns).
     */
    std::vector<flex_table_column_t> m_columns;

    /// Index of the geometry column in m_columns. Default means no geometry.
    std::size_t m_geom_column = std::numeric_limits<std::size_t>::max();

    /**
     * Type of Id stored in this table (node, way, relation, area, or
     * undefined for any type).
     */
    osmium::item_type m_id_type = osmium::item_type::undefined;

    /// The SRID all geometries in this table use.
    int m_srid;

    /**
     * The copy manager responsible for sending data through the COPY mechanism
     * to the database server.
     */
    db_copy_mgr_t<db_deleter_by_type_and_id_t> m_copy_mgr;

    /// The connection to the database server.
    std::unique_ptr<pg_conn_t> m_db_connection;

    std::shared_ptr<db_target_descr_t> m_target;

    /// Are we in append mode?
    bool m_append;

}; // class flex_table_t

char const *type_to_char(osmium::item_type type) noexcept;

#endif // OSM2PGSQL_FLEX_TABLE_HPP
