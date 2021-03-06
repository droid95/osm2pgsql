#include <libpq-fe.h>

#include "format.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "output-gazetteer.hpp"
#include "pgsql.hpp"
#include "util.hpp"
#include "wkb.hpp"

#include <cstring>
#include <iostream>
#include <memory>

void output_gazetteer_t::delete_unused_classes(char osm_type, osmid_t osm_id)
{
    if (m_options.append) {
        m_copy.prepare();

        assert(m_style.has_data());

        std::string cls = m_style.class_list();
        m_copy.delete_object(osm_type, osm_id, cls);
    }
}

void output_gazetteer_t::delete_unused_full(char osm_type, osmid_t osm_id)
{
    if (m_options.append) {
        m_copy.prepare();
        m_copy.delete_object(osm_type, osm_id);
    }
}

void output_gazetteer_t::start()
{
    int srid = m_options.projection->target_srs();

    /* (Re)create the table unless we are appending */
    if (!m_options.append) {
        pg_conn_t conn{m_options.database_options.conninfo()};

        /* Drop any existing table */
        conn.exec("DROP TABLE IF EXISTS place CASCADE");

        /* Create the new table */

        std::string sql =
            "CREATE TABLE place ("
            "  osm_id " POSTGRES_OSMID_TYPE " NOT NULL,"
            "  osm_type char(1) NOT NULL,"
            "  class TEXT NOT NULL,"
            "  type TEXT NOT NULL,"
            "  name HSTORE,"
            "  admin_level SMALLINT,"
            "  address HSTORE,"
            "  extratags HSTORE," +
            "  geometry Geometry(Geometry,{}) NOT NULL"_format(srid) + ")";
        if (m_options.tblsmain_data) {
            sql += " TABLESPACE " + m_options.tblsmain_data.get();
        }

        conn.exec(sql);

        std::string index_sql =
            "CREATE INDEX place_id_idx ON place USING BTREE (osm_type, osm_id)";
        if (m_options.tblsmain_index) {
            index_sql += " TABLESPACE " + m_options.tblsmain_index.get();
        }
        conn.exec(index_sql);
    }
}

void output_gazetteer_t::commit() { m_copy.sync(); }

void output_gazetteer_t::node_add(osmium::Node const &node)
{
    if (!process_node(node)) {
        delete_unused_full('N', node.id());
    }
}

void output_gazetteer_t::node_modify(osmium::Node const &node)
{
    if (!process_node(node)) {
        delete_unused_full('N', node.id());
    }
}

bool output_gazetteer_t::process_node(osmium::Node const &node)
{
    m_style.process_tags(node);

    /* Are we interested in this item? */
    if (!m_style.has_data()) {
        return false;
    }

    auto wkb = m_builder.get_wkb_node(node.location());
    delete_unused_classes('N', node.id());
    m_style.copy_out(node, wkb, m_copy);

    return true;
}

void output_gazetteer_t::way_add(osmium::Way *way)
{
    if (!process_way(way)) {
        delete_unused_full('W', way->id());
    }
}

void output_gazetteer_t::way_modify(osmium::Way *way)
{
    if (!process_way(way)) {
        delete_unused_full('W', way->id());
    }
}

bool output_gazetteer_t::process_way(osmium::Way *way)
{
    m_style.process_tags(*way);

    if (!m_style.has_data()) {
        return false;
    }

    // Fetch the node details.
    m_mid->nodes_get_list(&(way->nodes()));

    // Get the geometry of the object.
    geom::osmium_builder_t::wkb_t geom;
    if (way->is_closed()) {
        geom = m_builder.get_wkb_polygon(*way);
    }
    if (geom.empty()) {
        auto wkbs = m_builder.get_wkb_line(way->nodes(), 0.0);
        if (wkbs.empty()) {
            return false;
        }

        geom = wkbs[0];
    }

    delete_unused_classes('W', way->id());
    m_style.copy_out(*way, geom, m_copy);

    return true;
}

void output_gazetteer_t::relation_add(osmium::Relation const &rel)
{
    if (!process_relation(rel)) {
        delete_unused_full('R', rel.id());
    }
}

void output_gazetteer_t::relation_modify(osmium::Relation const &rel)
{
    if (!process_relation(rel)) {
        delete_unused_full('R', rel.id());
    }
}

bool output_gazetteer_t::process_relation(osmium::Relation const &rel)
{
    auto const &tags = rel.tags();
    char const *type = tags["type"];
    if (!type) {
        return false;
    }

    bool is_waterway = strcmp(type, "waterway") == 0;

    if (strcmp(type, "associatedStreet") == 0 ||
        !(strcmp(type, "boundary") == 0 || strcmp(type, "multipolygon") == 0 ||
          is_waterway)) {
        return false;
    }

    m_style.process_tags(rel);

    /* Are we interested in this item? */
    if (!m_style.has_data()) {
        return false;
    }

    /* get the boundary path (ways) */
    osmium_buffer.clear();
    auto num_ways = m_mid->rel_way_members_get(rel, nullptr, osmium_buffer);

    if (num_ways == 0) {
        return false;
    }

    for (auto &w : osmium_buffer.select<osmium::Way>()) {
        m_mid->nodes_get_list(&(w.nodes()));
    }

    auto geoms = is_waterway
                     ? m_builder.get_wkb_multiline(osmium_buffer, 0.0)
                     : m_builder.get_wkb_multipolygon(rel, osmium_buffer, true);

    if (geoms.empty()) {
        return false;
    }

    delete_unused_classes('R', rel.id());
    m_style.copy_out(rel, geoms[0], m_copy);

    return true;
}
