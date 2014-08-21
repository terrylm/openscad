#ifdef ENABLE_CGAL

#include "cgalutils.h"
#include "polyset.h"
#include "printutils.h"
#include "Polygon2d.h"
#include "polyset-utils.h"
#include "grid.h"
#include "node.h"

#include "cgal.h"
#include <CGAL/convex_hull_3.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/normal_vector_newell_3.h>
#include <CGAL/Handle_hash_function.h>
#include "svg.h"
#include "Reindexer.h"

#include <map>
#include <queue>
#include <boost/foreach.hpp>
#include <boost/unordered_set.hpp>

namespace /* anonymous */ {
	template<typename Result, typename V>
	Result vector_convert(V const& v) {
		return Result(CGAL::to_double(v[0]),CGAL::to_double(v[1]),CGAL::to_double(v[2]));
	}

#undef GEN_SURFACE_DEBUG

	namespace Eigen {
		size_t hash_value(Vector3d const &v) {
			size_t seed = 0;
			for (int i=0;i<3;i++) boost::hash_combine(seed, v[i]);
			return seed;
		}
	}

	class CGAL_Build_PolySet : public CGAL::Modifier_base<CGAL_HDS>
	{
	public:
		typedef CGAL_Polybuilder::Point_3 CGALPoint;

		const PolySet &ps;
		CGAL_Build_PolySet(const PolySet &ps) : ps(ps) { }

/*
	Using Grid here is important for performance reasons. See following model.
	If we don't grid the geometry before converting to a Nef Polyhedron, the quads
	in the cylinders to tessellated into triangles since floating point
	incertainty causes the faces to not be 100% planar. The incertainty is exaggerated
	by the transform. This wasn't a problem earlier since we used Nef for everything,
	but optimizations since then has made us keep it in floating point space longer.

  minkowski() {
	cube([200, 50, 7], center = true);
	rotate([90,0,0]) cylinder($fn = 8, h = 1, r = 8.36, center = true);
	rotate([0,90,0]) cylinder($fn = 8, h = 1, r = 8.36, center = true);
  }
*/
#if 1 // Use Grid
		void operator()(CGAL_HDS& hds) {
			CGAL_Polybuilder B(hds, true);
		
			std::vector<CGALPoint> vertices;
			Grid3d<int> grid(GRID_FINE);
			std::vector<size_t> indices(3);
		
			BOOST_FOREACH(const PolySet::Polygon &p, ps.polygons) {
				BOOST_REVERSE_FOREACH(Vector3d v, p) {
					if (!grid.has(v[0], v[1], v[2])) {
						// align v to the grid; the CGALPoint will receive the aligned vertex
						grid.align(v[0], v[1], v[2]) = vertices.size();
						vertices.push_back(CGALPoint(v[0], v[1], v[2]));
					}
				}
			}

#ifdef GEN_SURFACE_DEBUG
			printf("polyhedron(faces=[");
			int pidx = 0;
#endif
			B.begin_surface(vertices.size(), ps.polygons.size());
			BOOST_FOREACH(const CGALPoint &p, vertices) {
				B.add_vertex(p);
			}
			BOOST_FOREACH(const PolySet::Polygon &p, ps.polygons) {
#ifdef GEN_SURFACE_DEBUG
				if (pidx++ > 0) printf(",");
#endif
				indices.clear();
				BOOST_FOREACH(const Vector3d &v, p) {
					indices.push_back(grid.data(v[0], v[1], v[2]));
				}

				// We perform this test since there is a bug in CGAL's
				// Polyhedron_incremental_builder_3::test_facet() which
				// fails to detect duplicate indices
				bool err = false;
				for (std::size_t i = 0; i < indices.size(); ++i) {
					// check if vertex indices[i] is already in the sequence [0..i-1]
					for (std::size_t k = 0; k < i && !err; ++k) {
						if (indices[k] == indices[i]) {
							err = true;
							break;
						}
					}
				}
				if (!err && B.test_facet(indices.begin(), indices.end())) {
					B.add_facet(indices.begin(), indices.end());
				}
#ifdef GEN_SURFACE_DEBUG
				printf("[");
				int fidx = 0;
				BOOST_FOREACH(size_t i, indices) {
					if (fidx++ > 0) printf(",");
					printf("%ld", i);
				}
				printf("]");
#endif
			}
			B.end_surface();
#ifdef GEN_SURFACE_DEBUG
			printf("],\n");
#endif
#ifdef GEN_SURFACE_DEBUG
			printf("points=[");
			for (int i=0;i<vertices.size();i++) {
				if (i > 0) printf(",");
				const CGALPoint &p = vertices[i];
				printf("[%g,%g,%g]", CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
			}
			printf("]);\n");
#endif
		}
#else // Don't use Grid
		void operator()(CGAL_HDS& hds)
			{
				CGAL_Polybuilder B(hds, true);
				typedef boost::tuple<double, double, double> BuilderVertex;
				Reindexer<Vector3d> vertices;
				std::vector<size_t> indices(3);

				// Estimating same # of vertices as polygons (very rough)
				B.begin_surface(ps.polygons.size(), ps.polygons.size());
				int pidx = 0;
#ifdef GEN_SURFACE_DEBUG
				printf("polyhedron(faces=[");
#endif
				BOOST_FOREACH(const PolySet::Polygon &p, ps.polygons) {
#ifdef GEN_SURFACE_DEBUG
					if (pidx++ > 0) printf(",");
#endif
					indices.clear();
					BOOST_REVERSE_FOREACH(const Vector3d &v, p) {
						size_t s = vertices.size();
						size_t idx = vertices.lookup(v);
						// If we added a vertex, also add it to the CGAL builder
						if (idx == s) B.add_vertex(CGALPoint(v[0], v[1], v[2]));
						indices.push_back(idx);
					}
					// We perform this test since there is a bug in CGAL's
					// Polyhedron_incremental_builder_3::test_facet() which
					// fails to detect duplicate indices
					bool err = false;
					for (std::size_t i = 0; i < indices.size(); ++i) {
						// check if vertex indices[i] is already in the sequence [0..i-1]
						for (std::size_t k = 0; k < i && !err; ++k) {
							if (indices[k] == indices[i]) {
								err = true;
								break;
							}
						}
					}
					if (!err && B.test_facet(indices.begin(), indices.end())) {
						B.add_facet(indices.begin(), indices.end());
#ifdef GEN_SURFACE_DEBUG
						printf("[");
						int fidx = 0;
						BOOST_FOREACH(size_t i, indices) {
							if (fidx++ > 0) printf(",");
							printf("%ld", i);
						}
						printf("]");
#endif
					}
				}
				B.end_surface();
#ifdef GEN_SURFACE_DEBUG
				printf("],\n");

				printf("points=[");
				for (int vidx=0;vidx<vertices.size();vidx++) {
					if (vidx > 0) printf(",");
					const Vector3d &v = vertices.getArray()[vidx];
					printf("[%g,%g,%g]", v[0], v[1], v[2]);
				}
				printf("]);\n");
#endif
			}
#endif
	};

	// This code is from CGAL/demo/Polyhedron/Scene_nef_polyhedron_item.cpp
	// quick hacks to convert polyhedra from exact to inexact and vice-versa
	template <class Polyhedron_input,
	class Polyhedron_output>
	struct Copy_polyhedron_to
	: public CGAL::Modifier_base<typename Polyhedron_output::HalfedgeDS>
	{
		Copy_polyhedron_to(const Polyhedron_input& in_poly)
		: in_poly(in_poly) {}

		void operator()(typename Polyhedron_output::HalfedgeDS& out_hds)
		{
			typedef typename Polyhedron_output::HalfedgeDS Output_HDS;

			CGAL::Polyhedron_incremental_builder_3<Output_HDS> builder(out_hds);

			typedef typename Polyhedron_input::Vertex_const_iterator Vertex_const_iterator;
			typedef typename Polyhedron_input::Facet_const_iterator  Facet_const_iterator;
			typedef typename Polyhedron_input::Halfedge_around_facet_const_circulator HFCC;

			builder.begin_surface(in_poly.size_of_vertices(),
								  in_poly.size_of_facets(),
								  in_poly.size_of_halfedges());

			for(Vertex_const_iterator
				vi = in_poly.vertices_begin(), end = in_poly.vertices_end();
				vi != end ; ++vi)
			{
				typename Polyhedron_output::Point_3 p(::CGAL::to_double( vi->point().x()),
													  ::CGAL::to_double( vi->point().y()),
													  ::CGAL::to_double( vi->point().z()));
				builder.add_vertex(p);
			}

			typedef CGAL::Inverse_index<Vertex_const_iterator> Index;
			Index index( in_poly.vertices_begin(), in_poly.vertices_end());

			for(Facet_const_iterator
				fi = in_poly.facets_begin(), end = in_poly.facets_end();
				fi != end; ++fi)
			{
				HFCC hc = fi->facet_begin();
				HFCC hc_end = hc;
				//     std::size_t n = circulator_size( hc);
				//     CGAL_assertion( n >= 3);
				builder.begin_facet ();
				do {
					builder.add_vertex_to_facet(index[hc->vertex()]);
					++hc;
				} while( hc != hc_end);
				builder.end_facet();
			}
			builder.end_surface();
		} // end operator()(..)
	private:
		const Polyhedron_input& in_poly;
	}; // end Copy_polyhedron_to<>

	template <class Poly_A, class Poly_B>
	void copy_to(const Poly_A& poly_a, Poly_B& poly_b)
	{
		Copy_polyhedron_to<Poly_A, Poly_B> modifier(poly_a);
		poly_b.delegate(modifier);
	}

}

static CGAL_Nef_polyhedron *createNefPolyhedronFromPolySet(const PolySet &ps)
{
	if (ps.isEmpty()) return new CGAL_Nef_polyhedron();
	assert(ps.getDimension() == 3);

	if (ps.is_convex()) {
		typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
		// Collect point cloud
		std::set<K::Point_3> points;
		for (int i = 0; i < ps.polygons.size(); i++) {
			for (int j = 0; j < ps.polygons[i].size(); j++) {
				points.insert(vector_convert<K::Point_3>(ps.polygons[i][j]));
			}
		}

		if (points.size() <= 3) return new CGAL_Nef_polyhedron();;

		// Apply hull
		CGAL::Polyhedron_3<K> r;
		CGAL::convex_hull_3(points.begin(), points.end(), r);
		CGAL::Polyhedron_3<CGAL_Kernel3> r_exact;
		copy_to(r,r_exact);
		return new CGAL_Nef_polyhedron(new CGAL_Nef_polyhedron3(r_exact));
	}

	CGAL_Nef_polyhedron3 *N = NULL;
	bool plane_error = false;
	CGAL::Failure_behaviour old_behaviour = CGAL::set_error_behaviour(CGAL::THROW_EXCEPTION);
	try {
		CGAL_Polyhedron P;
		bool err = CGALUtils::createPolyhedronFromPolySet(ps, P);
		// if (!err) {
		// 	PRINTB("Polyhedron is closed: %d", P.is_closed());
		// 	PRINTB("Polyhedron is valid: %d", P.is_valid(true, 0));
		// }

		if (!err) N = new CGAL_Nef_polyhedron3(P);
	}
	catch (const CGAL::Assertion_exception &e) {
		if (std::string(e.what()).find("Plane_constructor")!=std::string::npos) {
			if (std::string(e.what()).find("has_on")!=std::string::npos) {
				PRINT("PolySet has nonplanar faces. Attempting alternate construction");
				plane_error=true;
			}
		} else {
			PRINTB("CGAL error in CGAL_Nef_polyhedron3(): %s", e.what());
		}
	}
	if (plane_error) try {
			PolySet ps2(3);
			CGAL_Polyhedron P;
			PolysetUtils::tessellate_faces(ps, ps2);
			bool err = CGALUtils::createPolyhedronFromPolySet(ps2,P);
			if (!err) N = new CGAL_Nef_polyhedron3(P);
		}
		catch (const CGAL::Assertion_exception &e) {
			PRINTB("Alternate construction failed. CGAL error in CGAL_Nef_polyhedron3(): %s", e.what());
		}
	CGAL::set_error_behaviour(old_behaviour);
	return new CGAL_Nef_polyhedron(N);
}

static CGAL_Nef_polyhedron *createNefPolyhedronFromPolygon2d(const Polygon2d &polygon)
{
	shared_ptr<PolySet> ps(polygon.tessellate());
	return createNefPolyhedronFromPolySet(*ps);
}

namespace CGALUtils {

	bool createPolyhedronFromPolySet(const PolySet &ps, CGAL_Polyhedron &p)
	{
		bool err = false;
		CGAL::Failure_behaviour old_behaviour = CGAL::set_error_behaviour(CGAL::THROW_EXCEPTION);
		try {
			CGAL_Build_PolySet builder(ps);
			p.delegate(builder);
		}
		catch (const CGAL::Assertion_exception &e) {
			PRINTB("CGAL error in CGALUtils::createPolyhedronFromPolySet: %s", e.what());
			err = true;
		}
		CGAL::set_error_behaviour(old_behaviour);
		return err;
	}

	bool applyHull(const Geometry::ChildList &children, PolySet &result)
	{
		typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
		// Collect point cloud
		std::set<K::Point_3> points;

		BOOST_FOREACH(const Geometry::ChildItem &item, children) {
			const shared_ptr<const Geometry> &chgeom = item.second;
			const CGAL_Nef_polyhedron *N = dynamic_cast<const CGAL_Nef_polyhedron *>(chgeom.get());
			if (N) {
				for (CGAL_Nef_polyhedron3::Vertex_const_iterator i = N->p3->vertices_begin(); i != N->p3->vertices_end(); ++i) {
					points.insert(K::Point_3(to_double(i->point()[0]),to_double(i->point()[1]),to_double(i->point()[2])));
				}
			} else {
				const PolySet *ps = dynamic_cast<const PolySet *>(chgeom.get());
				if (ps) {
					BOOST_FOREACH(const PolySet::Polygon &p, ps->polygons) {
						BOOST_FOREACH(const Vector3d &v, p) {
							points.insert(K::Point_3(v[0], v[1], v[2]));
						}
					}
				}
			}
		}

		if (points.size() <= 3) return false;

		// Apply hull
		if (points.size() >= 4) {
			CGAL::Polyhedron_3<K> r;
			CGAL::convex_hull_3(points.begin(), points.end(), r);
			if (!createPolySetFromPolyhedron(r, result))
				return true;
			return false;
		} else {
			return false;
		}
	}

	template<typename Polyhedron>
	bool is_weakly_convex(Polyhedron const& p) {
		for (typename Polyhedron::Edge_const_iterator i = p.edges_begin(); i != p.edges_end(); ++i) {
			typename Polyhedron::Plane_3 p(i->opposite()->vertex()->point(), i->vertex()->point(), i->next()->vertex()->point());
			if (p.has_on_positive_side(i->opposite()->next()->vertex()->point()) &&
				CGAL::squared_distance(p, i->opposite()->next()->vertex()->point()) > 1e-8) {
				return false;
			}
		}
		// Also make sure that there is only one shell:
		boost::unordered_set<typename Polyhedron::Facet_const_handle, typename CGAL::Handle_hash_function> visited;
		// c++11
		// visited.reserve(p.size_of_facets());

		std::queue<typename Polyhedron::Facet_const_handle> to_explore;
		to_explore.push(p.facets_begin()); // One arbitrary facet
		visited.insert(to_explore.front());

		while (!to_explore.empty()) {
			typename Polyhedron::Facet_const_handle f = to_explore.front();
			to_explore.pop();
			typename Polyhedron::Facet::Halfedge_around_facet_const_circulator he, end;
			end = he = f->facet_begin();
			CGAL_For_all(he,end) {
				typename Polyhedron::Facet_const_handle o = he->opposite()->facet();

				if (!visited.count(o)) {
					visited.insert(o);
					to_explore.push(o);
				}
			}
		}

		return visited.size() == p.size_of_facets();
	}

	Geometry const * applyMinkowski(const Geometry::ChildList &children)
	{
		CGAL::Timer t,t_tot;
		assert(children.size() >= 2);
		Geometry::ChildList::const_iterator it = children.begin();
		t_tot.start();
		Geometry const* operands[2] = {it->second.get(), NULL};
		try {
			while (++it != children.end()) {
				operands[1] = it->second.get();

				typedef CGAL::Exact_predicates_inexact_constructions_kernel Hull_kernel;

				std::list<CGAL_Polyhedron> P[2];
				std::list<CGAL::Polyhedron_3<Hull_kernel> > result_parts;

				for (int i = 0; i < 2; i++) {
					CGAL_Polyhedron poly;

					const PolySet * ps = dynamic_cast<const PolySet *>(operands[i]);

					const CGAL_Nef_polyhedron * nef = dynamic_cast<const CGAL_Nef_polyhedron *>(operands[i]);

					if (ps) CGALUtils::createPolyhedronFromPolySet(*ps, poly);
					else if (nef && nef->p3->is_simple()) nefworkaround::convert_to_Polyhedron<CGAL_Kernel3>(*nef->p3, poly);
					else throw 0;

					if ((ps && ps->is_convex()) ||
							(!ps && is_weakly_convex(poly))) {
						PRINTDB("Minkowski: child %d is convex and %s",i % (ps?"PolySet":"Nef") );
						P[i].push_back(poly);
					} else {
						CGAL_Nef_polyhedron3 decomposed_nef;

						if (ps) {
							PRINTDB("Minkowski: child %d is nonconvex PolySet, transforming to Nef and decomposing...", i);
							CGAL_Nef_polyhedron *p = createNefPolyhedronFromGeometry(*ps);
							decomposed_nef = *p->p3;
							delete p;
						} else {
							PRINTDB("Minkowski: child %d is nonconvex Nef, decomposing...",i);
							decomposed_nef = *nef->p3;
						}

						CGAL::convex_decomposition_3(decomposed_nef);

						// the first volume is the outer volume, which ignored in the decomposition
						CGAL_Nef_polyhedron3::Volume_const_iterator ci = ++decomposed_nef.volumes_begin();
						for( ; ci != decomposed_nef.volumes_end(); ++ci) {
							if(ci->mark()) {
								CGAL_Polyhedron poly;
								decomposed_nef.convert_inner_shell_to_polyhedron(ci->shells_begin(), poly);
								P[i].push_back(poly);
							}
						}


						PRINTDB("Minkowski: decomposed into %d convex parts", P[i].size());
					}
				}

				std::vector<Hull_kernel::Point_3> points[2];
				std::vector<Hull_kernel::Point_3> minkowski_points;

				for (int i = 0; i < P[0].size(); i++) {
					for (int j = 0; j < P[1].size(); j++) {
						t.start();
						points[0].clear();
						points[1].clear();

						for (int k = 0; k < 2; k++) {
							std::list<CGAL_Polyhedron>::iterator it = P[k].begin();
							std::advance(it, k==0?i:j);

							CGAL_Polyhedron const& poly = *it;
							points[k].reserve(poly.size_of_vertices());

							for (CGAL_Polyhedron::Vertex_const_iterator pi = poly.vertices_begin(); pi != poly.vertices_end(); ++pi) {
								CGAL_Polyhedron::Point_3 const& p = pi->point();
								points[k].push_back(Hull_kernel::Point_3(to_double(p[0]),to_double(p[1]),to_double(p[2])));
							}
						}

						minkowski_points.clear();
						minkowski_points.reserve(points[0].size() * points[1].size());
						for (int i = 0; i < points[0].size(); i++) {
							for (int j = 0; j < points[1].size(); j++) {
								minkowski_points.push_back(points[0][i]+(points[1][j]-CGAL::ORIGIN));
							}
						}

						if (minkowski_points.size() <= 3) {
							t.stop();
							continue;
						}


						CGAL::Polyhedron_3<Hull_kernel> result;
						t.stop();
						PRINTDB("Minkowski: Point cloud creation (%d ⨉ %d -> %d) took %f ms", points[0].size() % points[1].size() % minkowski_points.size() % (t.time()*1000));
						t.reset();

						t.start();

						CGAL::convex_hull_3(minkowski_points.begin(), minkowski_points.end(), result);

						std::vector<Hull_kernel::Point_3> strict_points;
						strict_points.reserve(minkowski_points.size());

						for (CGAL::Polyhedron_3<Hull_kernel>::Vertex_iterator i = result.vertices_begin(); i != result.vertices_end(); ++i) {
							Hull_kernel::Point_3 const& p = i->point();

							CGAL::Polyhedron_3<Hull_kernel>::Vertex::Halfedge_handle h,e;
							h = i->halfedge();
							e = h;
							bool collinear = false;
							bool coplanar = true;

							do {
								Hull_kernel::Point_3 const& q = h->opposite()->vertex()->point();
								if (coplanar && !CGAL::coplanar(p,q,
																h->next_on_vertex()->opposite()->vertex()->point(),
																h->next_on_vertex()->next_on_vertex()->opposite()->vertex()->point())) {
									coplanar = false;
								}


								for (CGAL::Polyhedron_3<Hull_kernel>::Vertex::Halfedge_handle j = h->next_on_vertex();
									 j != h && !collinear && ! coplanar;
									 j = j->next_on_vertex()) {

									Hull_kernel::Point_3 const& r = j->opposite()->vertex()->point();
									if (CGAL::collinear(p,q,r)) {
										collinear = true;
									}
								}

								h = h->next_on_vertex();
							} while (h != e && !collinear);

							if (!collinear && !coplanar)
								strict_points.push_back(p);
						}

						result.clear();
						CGAL::convex_hull_3(strict_points.begin(), strict_points.end(), result);


						t.stop();
						PRINTDB("Minkowski: Computing convex hull took %f s", t.time());
						t.reset();

						result_parts.push_back(result);
					}
				}

				if (it != boost::next(children.begin()))
					delete operands[0];

				if (result_parts.size() == 1) {
					PolySet *ps = new PolySet(3,true);
					createPolySetFromPolyhedron(*result_parts.begin(), *ps);
					operands[0] = ps;
				} else if (!result_parts.empty()) {
					t.start();
					PRINTDB("Minkowski: Computing union of %d parts",result_parts.size());
					Geometry::ChildList fake_children;
					for (std::list<CGAL::Polyhedron_3<Hull_kernel> >::iterator i = result_parts.begin(); i != result_parts.end(); ++i) {
						PolySet ps(3,true);
						createPolySetFromPolyhedron(*i, ps);
						fake_children.push_back(std::make_pair((const AbstractNode*)NULL,
															   shared_ptr<const Geometry>(createNefPolyhedronFromGeometry(ps))));
					}
					CGAL_Nef_polyhedron *N = new CGAL_Nef_polyhedron;
					CGALUtils::applyOperator(fake_children, *N, OPENSCAD_UNION);
					t.stop();
					PRINTDB("Minkowski: Union done: %f s",t.time());
					t.reset();
					operands[0] = N;
				} else {
					return NULL;
				}
			}

			t_tot.stop();
			PRINTDB("Minkowski: Total execution time %f s", t_tot.time());
			t_tot.reset();
			return operands[0];
		}
		catch (...) {
			// If anything throws we simply fall back to Nef Minkowski
			PRINTD("Minkowski: Falling back to Nef Minkowski");

			CGAL_Nef_polyhedron *N = new CGAL_Nef_polyhedron;
			applyOperator(children, *N, OPENSCAD_MINKOWSKI);
			return N;
		}
	}
	
/*!
	Applies op to all children and stores the result in dest.
	The child list should be guaranteed to contain non-NULL 3D or empty Geometry objects
*/
	void applyOperator(const Geometry::ChildList &children, CGAL_Nef_polyhedron &dest, OpenSCADOperator op)
	{
		// Speeds up n-ary union operations significantly
		CGAL::Nef_nary_union_3<CGAL_Nef_polyhedron3> nary_union;
		int nary_union_num_inserted = 0;
		CGAL_Nef_polyhedron *N = NULL;

		BOOST_FOREACH(const Geometry::ChildItem &item, children) {
			const shared_ptr<const Geometry> &chgeom = item.second;
			shared_ptr<const CGAL_Nef_polyhedron> chN = 
				dynamic_pointer_cast<const CGAL_Nef_polyhedron>(chgeom);
			if (!chN) {
				const PolySet *chps = dynamic_cast<const PolySet*>(chgeom.get());
				if (chps) chN.reset(createNefPolyhedronFromGeometry(*chps));
			}

			if (op == OPENSCAD_UNION) {
				if (!chN->isEmpty()) {
					// nary_union.add_polyhedron() can issue assertion errors:
					// https://github.com/openscad/openscad/issues/802
					CGAL::Failure_behaviour old_behaviour = CGAL::set_error_behaviour(CGAL::THROW_EXCEPTION);
					try {
						nary_union.add_polyhedron(*chN->p3);
						nary_union_num_inserted++;
					}
					catch (const CGAL::Failure_exception &e) {
						PRINTB("CGAL error in CGALUtils::applyBinaryOperator union: %s", e.what());
					}
					CGAL::set_error_behaviour(old_behaviour);
				}
				continue;
			}
			// Initialize N with first expected geometric object
			if (!N) {
				N = new CGAL_Nef_polyhedron(*chN);
				continue;
			}

			// Intersecting something with nothing results in nothing
			if (chN->isEmpty()) {
				if (op == OPENSCAD_INTERSECTION) *N = *chN;
				continue;
			}
            
            // empty op <something> => empty
            if (N->isEmpty()) continue;

			CGAL::Failure_behaviour old_behaviour = CGAL::set_error_behaviour(CGAL::THROW_EXCEPTION);
			try {
				switch (op) {
				case OPENSCAD_INTERSECTION:
					*N *= *chN;
					break;
				case OPENSCAD_DIFFERENCE:
					*N -= *chN;
					break;
				case OPENSCAD_MINKOWSKI:
					N->minkowski(*chN);
					break;
				default:
					PRINTB("ERROR: Unsupported CGAL operator: %d", op);
				}
			}
			catch (const CGAL::Failure_exception &e) {
				// union && difference assert triggered by testdata/scad/bugs/rotate-diff-nonmanifold-crash.scad and testdata/scad/bugs/issue204.scad
				std::string opstr = op == OPENSCAD_INTERSECTION ? "intersection" : op == OPENSCAD_DIFFERENCE ? "difference" : op == OPENSCAD_MINKOWSKI ? "minkowski" : "UNKNOWN";
				PRINTB("CGAL error in CGALUtils::applyBinaryOperator %s: %s", opstr % e.what());
				
				// Errors can result in corrupt polyhedrons, so put back the old one
				*N = *chN;
			}
			CGAL::set_error_behaviour(old_behaviour);
			item.first->progress_report();
		}

		if (op == OPENSCAD_UNION && nary_union_num_inserted > 0) {
			CGAL::Failure_behaviour old_behaviour = CGAL::set_error_behaviour(CGAL::THROW_EXCEPTION);
			try {

				N = new CGAL_Nef_polyhedron(new CGAL_Nef_polyhedron3(nary_union.get_union()));

			} catch (const CGAL::Failure_exception &e) {
				std::string opstr = "union";
				PRINTB("CGAL error in CGALUtils::applyBinaryOperator %s: %s", opstr % e.what());
			}
			CGAL::set_error_behaviour(old_behaviour);
		}
		if (N) dest = *N;
	}

/*!
	Modifies target by applying op to target and src:
	target = target [op] src
*/
	void applyBinaryOperator(CGAL_Nef_polyhedron &target, const CGAL_Nef_polyhedron &src, OpenSCADOperator op)
	{
		if (target.getDimension() != 2 && target.getDimension() != 3) {
			assert(false && "Dimension of Nef polyhedron must be 2 or 3");
		}
		if (src.isEmpty()) {
			// Intersecting something with nothing results in nothing
			if (op == OPENSCAD_INTERSECTION) target = src;
			// else keep target unmodified
			return;
		}
		if (src.isEmpty()) return; // Empty polyhedron. This can happen for e.g. square([0,0])
		if (target.isEmpty() && op != OPENSCAD_UNION) return; // empty op <something> => empty
		if (target.getDimension() != src.getDimension()) return; // If someone tries to e.g. union 2d and 3d objects

		CGAL::Failure_behaviour old_behaviour = CGAL::set_error_behaviour(CGAL::THROW_EXCEPTION);
		try {
			switch (op) {
			case OPENSCAD_UNION:
				if (target.isEmpty()) target = *new CGAL_Nef_polyhedron(src);
				else target += src;
				break;
			case OPENSCAD_INTERSECTION:
				target *= src;
				break;
			case OPENSCAD_DIFFERENCE:
				target -= src;
				break;
			case OPENSCAD_MINKOWSKI:
				target.minkowski(src);
				break;
			default:
				PRINTB("ERROR: Unsupported CGAL operator: %d", op);
			}
		}
		catch (const CGAL::Failure_exception &e) {
			// union && difference assert triggered by testdata/scad/bugs/rotate-diff-nonmanifold-crash.scad and testdata/scad/bugs/issue204.scad
			std::string opstr = op == OPENSCAD_UNION ? "union" : op == OPENSCAD_INTERSECTION ? "intersection" : op == OPENSCAD_DIFFERENCE ? "difference" : op == OPENSCAD_MINKOWSKI ? "minkowski" : "UNKNOWN";
			PRINTB("CGAL error in CGALUtils::applyBinaryOperator %s: %s", opstr % e.what());

			// Errors can result in corrupt polyhedrons, so put back the old one
			target = src;
		}
		CGAL::set_error_behaviour(old_behaviour);
	}

	static void add_outline_to_poly(CGAL_Nef_polyhedron2::Explorer &explorer,
									CGAL_Nef_polyhedron2::Explorer::Halfedge_around_face_const_circulator circ,
									CGAL_Nef_polyhedron2::Explorer::Halfedge_around_face_const_circulator end,
									bool positive,
									Polygon2d *poly) {
		Outline2d outline;

		CGAL_For_all(circ, end) {
			if (explorer.is_standard(explorer.target(circ))) {
				CGAL_Nef_polyhedron2::Explorer::Point ep = explorer.point(explorer.target(circ));
				outline.vertices.push_back(Vector2d(to_double(ep.x()),
													to_double(ep.y())));
			}
		}

		if (!outline.vertices.empty()) {
			outline.positive = positive;
			poly->addOutline(outline);
		}
	}

	static Polygon2d *convertToPolygon2d(const CGAL_Nef_polyhedron2 &p2)
	{
		Polygon2d *poly = new Polygon2d;
		
		typedef CGAL_Nef_polyhedron2::Explorer Explorer;
		typedef Explorer::Face_const_iterator fci_t;
		typedef Explorer::Halfedge_around_face_const_circulator heafcc_t;
		Explorer E = p2.explorer();

		for (fci_t fit = E.faces_begin(), facesend = E.faces_end(); fit != facesend; ++fit)	{
			if (!fit->mark()) continue;

			heafcc_t fcirc(E.face_cycle(fit)), fend(fcirc);

			add_outline_to_poly(E, fcirc, fend, true, poly);

			for (CGAL_Nef_polyhedron2::Explorer::Hole_const_iterator j = E.holes_begin(fit);
				 j != E.holes_end(fit); ++j) {
				CGAL_Nef_polyhedron2::Explorer::Halfedge_around_face_const_circulator hcirc(j), hend(hcirc);

				add_outline_to_poly(E, hcirc, hend, false, poly);
			}
		}

		poly->setSanitized(true);
		return poly;
	}

	Polygon2d *project(const CGAL_Nef_polyhedron &N, bool cut)
	{
		Polygon2d *poly = NULL;
		if (N.getDimension() != 3) return poly;

		CGAL_Nef_polyhedron newN;
		if (cut) {
			CGAL::Failure_behaviour old_behaviour = CGAL::set_error_behaviour(CGAL::THROW_EXCEPTION);
			try {
				CGAL_Nef_polyhedron3::Plane_3 xy_plane = CGAL_Nef_polyhedron3::Plane_3(0,0,1,0);
				newN.p3.reset(new CGAL_Nef_polyhedron3(N.p3->intersection(xy_plane, CGAL_Nef_polyhedron3::PLANE_ONLY)));
			}
			catch (const CGAL::Failure_exception &e) {
				PRINTDB("CGALUtils::project during plane intersection: %s", e.what());
				try {
					PRINTD("Trying alternative intersection using very large thin box: ");
					std::vector<CGAL_Point_3> pts;
					// dont use z of 0. there are bugs in CGAL.
					double inf = 1e8;
					double eps = 0.001;
					CGAL_Point_3 minpt( -inf, -inf, -eps );
					CGAL_Point_3 maxpt(  inf,  inf,  eps );
					CGAL_Iso_cuboid_3 bigcuboid( minpt, maxpt );
					for ( int i=0;i<8;i++ ) pts.push_back( bigcuboid.vertex(i) );
					CGAL_Polyhedron bigbox;
					CGAL::convex_hull_3(pts.begin(), pts.end(), bigbox);
					CGAL_Nef_polyhedron3 nef_bigbox( bigbox );
					newN.p3.reset(new CGAL_Nef_polyhedron3(nef_bigbox.intersection(*N.p3)));
				}
				catch (const CGAL::Failure_exception &e) {
					PRINTB("CGAL error in CGALUtils::project during bigbox intersection: %s", e.what());
				}
			}
				
			if (!newN.p3 || newN.p3->is_empty()) {
				CGAL::set_error_behaviour(old_behaviour);
				PRINT("WARNING: projection() failed.");
				return poly;
			}
				
			PRINTDB("%s",OpenSCAD::svg_header( 480, 100000 ));
			try {
				ZRemover zremover;
				CGAL_Nef_polyhedron3::Volume_const_iterator i;
				CGAL_Nef_polyhedron3::Shell_entry_const_iterator j;
				CGAL_Nef_polyhedron3::SFace_const_handle sface_handle;
				for ( i = newN.p3->volumes_begin(); i != newN.p3->volumes_end(); ++i ) {
					PRINTDB("<!-- volume. mark: %s -->",i->mark());
					for ( j = i->shells_begin(); j != i->shells_end(); ++j ) {
						PRINTDB("<!-- shell. (vol mark was: %i)", i->mark());;
						sface_handle = CGAL_Nef_polyhedron3::SFace_const_handle( j );
						newN.p3->visit_shell_objects( sface_handle , zremover );
						PRINTD("<!-- shell. end. -->");
					}
					PRINTD("<!-- volume end. -->");
				}
				poly = convertToPolygon2d(*zremover.output_nefpoly2d);
			}	catch (const CGAL::Failure_exception &e) {
				PRINTB("CGAL error in CGALUtils::project while flattening: %s", e.what());
			}
			PRINTD("</svg>");
				
			CGAL::set_error_behaviour(old_behaviour);
		}
		// In projection mode all the triangles are projected manually into the XY plane
		else {
			PolySet *ps3 = N.convertToPolyset();
			if (!ps3) return poly;
			poly = PolysetUtils::project(*ps3);
			delete ps3;
		}
		return poly;
	}

	CGAL_Iso_cuboid_3 boundingBox(const CGAL_Nef_polyhedron3 &N)
	{
		CGAL_Iso_cuboid_3 result(0,0,0,0,0,0);
		CGAL_Nef_polyhedron3::Vertex_const_iterator vi;
		std::vector<CGAL_Nef_polyhedron3::Point_3> points;
		// can be optimized by rewriting bounding_box to accept vertices
		CGAL_forall_vertices(vi, N)
		points.push_back(vi->point());
		if (points.size()) result = CGAL::bounding_box( points.begin(), points.end() );
		return result;
	}

	namespace {

		// lexicographic comparison
		bool operator < (Vector3d const& a, Vector3d const& b) {
			for (int i = 0; i < 3; i++) {
				if (a[i] < b[i]) return true;
				else if (a[i] == b[i]) continue;
				return false;
			}
			return false;
		}
	}

	struct VecPairCompare {
		bool operator ()(std::pair<Vector3d, Vector3d> const& a,
						 std::pair<Vector3d, Vector3d> const& b) const {
			return a.first < b.first || (!(b.first < a.first) && a.second < b.second);
		}
	};


	bool is_approximately_convex(const PolySet &ps) {

		const double angle_threshold = cos(.1/180*M_PI); // .1°

		typedef CGAL::Simple_cartesian<double> K;
		typedef K::Vector_3 Vector;
		typedef K::Point_3 Point;
		typedef K::Plane_3 Plane;

		// compute edge to face relations and plane equations
		typedef std::pair<Vector3d,Vector3d> Edge;
		typedef std::map<Edge, int, VecPairCompare> Edge_to_facet_map;
		Edge_to_facet_map edge_to_facet_map;
		std::vector<Plane> facet_planes; facet_planes.reserve(ps.polygons.size());

		for (int i = 0; i < ps.polygons.size(); i++) {
			Plane plane;
			size_t N = ps.polygons[i].size();
			if (N >= 3) {
				std::vector<Point> v(N);
				for (int j = 0; j < N; j++) {
					v[j] = vector_convert<Point>(ps.polygons[i][j]);
					Edge edge(ps.polygons[i][j],ps.polygons[i][(j+1)%N]);
					if (edge_to_facet_map.count(edge)) return false; // edge already exists: nonmanifold
					edge_to_facet_map[edge] = i;
				}
				Vector normal;
				CGAL::normal_vector_newell_3(v.begin(), v.end(), normal);
				plane = Plane(v[0], normal);
			}
			facet_planes.push_back(plane);
		}

		for (int i = 0; i < ps.polygons.size(); i++) {
			size_t N = ps.polygons[i].size();
			if (N < 3) continue;
			for (int j = 0; j < N; j++) {
				Edge other_edge(ps.polygons[i][(j+1)%N], ps.polygons[i][j]);
				if (edge_to_facet_map.count(other_edge) == 0) return false;//
				//Edge_to_facet_map::const_iterator it = edge_to_facet_map.find(other_edge);
				//if (it == edge_to_facet_map.end()) return false; // not a closed manifold
				//int other_facet = it->second;
				int other_facet = edge_to_facet_map[other_edge];

				Point p = vector_convert<Point>(ps.polygons[i][(j+2)%N]);

				if (facet_planes[other_facet].has_on_positive_side(p)) {
					// Check angle
					Vector u = facet_planes[other_facet].orthogonal_vector();
					Vector v = facet_planes[i].orthogonal_vector();

					double cos_angle = u / sqrt(u*u) * v / sqrt(v*v);
					if (cos_angle < angle_threshold) {
						return false;
					}
				}
			}
		}

		std::set<int> explored_facets;
		std::queue<int> facets_to_visit;
		facets_to_visit.push(0);
		explored_facets.insert(0);

		while(!facets_to_visit.empty()) {
			int f = facets_to_visit.front(); facets_to_visit.pop();

			for (int i = 0; i < ps.polygons[f].size(); i++) {
				int j = (i+1) % ps.polygons[f].size();
				Edge_to_facet_map::iterator it = edge_to_facet_map.find(Edge(ps.polygons[f][i], ps.polygons[f][j]));
				if (it == edge_to_facet_map.end()) return false; // Nonmanifold
				if (!explored_facets.count(it->second)) {
					explored_facets.insert(it->second);
					facets_to_visit.push(it->second);
				}
			}
		}

		// Make sure that we were able to reach all polygons during our visit
		return explored_facets.size() == ps.polygons.size();
	}

	template <typename Polyhedron>
	bool createPolySetFromPolyhedron(const Polyhedron &p, PolySet &ps)
	{
		bool err = false;
		typedef typename Polyhedron::Vertex                                 Vertex;
		typedef typename Polyhedron::Vertex_const_iterator                  VCI;
		typedef typename Polyhedron::Facet_const_iterator                   FCI;
		typedef typename Polyhedron::Halfedge_around_facet_const_circulator HFCC;
		
		for (FCI fi = p.facets_begin(); fi != p.facets_end(); ++fi) {
			HFCC hc = fi->facet_begin();
			HFCC hc_end = hc;
			ps.append_poly();
			do {
				Vertex const& v = *((hc++)->vertex());
				double x = CGAL::to_double(v.point().x());
				double y = CGAL::to_double(v.point().y());
				double z = CGAL::to_double(v.point().z());
				ps.append_vertex(x, y, z);
			} while (hc != hc_end);
		}
		return err;
	}

	template bool createPolySetFromPolyhedron(const CGAL_Polyhedron &p, PolySet &ps);
	template bool createPolySetFromPolyhedron(const CGAL::Polyhedron_3<CGAL::Epick> &p, PolySet &ps);
	template bool createPolySetFromPolyhedron(const CGAL::Polyhedron_3<CGAL::Epeck> &p, PolySet &ps);
	template bool createPolySetFromPolyhedron(const CGAL::Polyhedron_3<CGAL::Simple_cartesian<long> > &p, PolySet &ps);

/*
	Create a PolySet from a Nef Polyhedron 3. return false on success, 
	true on failure. The trick to this is that Nef Polyhedron3 faces have 
	'holes' in them. . . while PolySet (and many other 3d polyhedron 
	formats) do not allow for holes in their faces. The function documents 
	the method used to deal with this
*/
	bool createPolySetFromNefPolyhedron3(const CGAL_Nef_polyhedron3 &N, PolySet &ps)
	{
		bool err = false;
		CGAL_Nef_polyhedron3::Halffacet_const_iterator hfaceti;
		CGAL_forall_halffacets( hfaceti, N ) {
			CGAL::Plane_3<CGAL_Kernel3> plane( hfaceti->plane() );
			std::vector<CGAL_Polygon_3> polygons;
			// the 0-mark-volume is the 'empty' volume of space. skip it.
			if (hfaceti->incident_volume()->mark()) continue;
			CGAL_Nef_polyhedron3::Halffacet_cycle_const_iterator cyclei;
			CGAL_forall_facet_cycles_of( cyclei, hfaceti ) {
				CGAL_Nef_polyhedron3::SHalfedge_around_facet_const_circulator c1(cyclei);
				CGAL_Nef_polyhedron3::SHalfedge_around_facet_const_circulator c2(c1);
				CGAL_Polygon_3 polygon;
				CGAL_For_all( c1, c2 ) {
					CGAL_Point_3 p = c1->source()->center_vertex()->point();
					polygon.push_back( p );
				}
				polygons.push_back( polygon );
			}

			/* at this stage, we have a sequence of polygons. the first
				 is the "outside edge' or 'body' or 'border', and the rest of the
				 polygons are 'holes' within the first. there are several
				 options here to get rid of the holes. we choose to go ahead
				 and let the tessellater deal with the holes, and then
				 just output the resulting 3d triangles*/
			std::vector<CGAL_Polygon_3> triangles;
			bool err = CGALUtils::tessellate3DFaceWithHoles(polygons, triangles, plane);
			if (!err) {
				for (size_t i=0;i<triangles.size();i++) {
					if (triangles[i].size()!=3) {
						PRINT("WARNING: triangle doesn't have 3 points. skipping");
						continue;
					}
					ps.append_poly();
					for (int j=2;j>=0;j--) {
						double x1,y1,z1;
						x1 = CGAL::to_double(triangles[i][j].x());
						y1 = CGAL::to_double(triangles[i][j].y());
						z1 = CGAL::to_double(triangles[i][j].z());
						ps.append_vertex(x1,y1,z1);
					}
				}
			}
		}
		return err;
	}

	CGAL_Nef_polyhedron *createNefPolyhedronFromGeometry(const Geometry &geom)
	{
		const PolySet *ps = dynamic_cast<const PolySet*>(&geom);
		if (ps) {
			return createNefPolyhedronFromPolySet(*ps);
		}
		else {
			const Polygon2d *poly2d = dynamic_cast<const Polygon2d*>(&geom);
			if (poly2d) return createNefPolyhedronFromPolygon2d(*poly2d);
		}
		assert(false && "createNefPolyhedronFromGeometry(): Unsupported geometry type");
		return NULL;
	}
}; // namespace CGALUtils


void ZRemover::visit( CGAL_Nef_polyhedron3::Halffacet_const_handle hfacet )
{
	PRINTDB(" <!-- ZRemover Halffacet visit. Mark: %i --> ",hfacet->mark());
	if ( hfacet->plane().orthogonal_direction() != this->up ) {
		PRINTD("  <!-- ZRemover down-facing half-facet. skipping -->");
		PRINTD(" <!-- ZRemover Halffacet visit end-->");
		return;
	}

	// possible optimization - throw out facets that are vertically oriented

	CGAL_Nef_polyhedron3::Halffacet_cycle_const_iterator fci;
	int contour_counter = 0;
	CGAL_forall_facet_cycles_of( fci, hfacet ) {
		if ( fci.is_shalfedge() ) {
			PRINTD(" <!-- ZRemover Halffacet cycle begin -->");
			CGAL_Nef_polyhedron3::SHalfedge_around_facet_const_circulator c1(fci), cend(c1);
			std::vector<CGAL_Nef_polyhedron2::Explorer::Point> contour;
			CGAL_For_all( c1, cend ) {
				CGAL_Nef_polyhedron3::Point_3 point3d = c1->source()->target()->point();
				CGAL_Nef_polyhedron2::Explorer::Point point2d(CGAL::to_double(point3d.x()),
																											CGAL::to_double(point3d.y()));
				contour.push_back( point2d );
			}
			if (contour.size()==0) continue;

			if (OpenSCAD::debug!="")
				PRINTDB(" <!-- is_simple_2: %i -->", CGAL::is_simple_2( contour.begin(), contour.end() ) );

			tmpnef2d.reset( new CGAL_Nef_polyhedron2( contour.begin(), contour.end(), boundary ) );

			if ( contour_counter == 0 ) {
				PRINTDB(" <!-- contour is a body. make union(). %i  points -->", contour.size() );
				*(output_nefpoly2d) += *(tmpnef2d);
			} else {
				PRINTDB(" <!-- contour is a hole. make intersection(). %i  points -->", contour.size() );
				*(output_nefpoly2d) *= *(tmpnef2d);
			}

			/*log << "\n<!-- ======== output tmp nef: ==== -->\n"
				<< OpenSCAD::dump_svg( *tmpnef2d ) << "\n"
				<< "\n<!-- ======== output accumulator: ==== -->\n"
				<< OpenSCAD::dump_svg( *output_nefpoly2d ) << "\n";*/

			contour_counter++;
		} else {
			PRINTD(" <!-- ZRemover trivial facet cycle skipped -->");
		}
		PRINTD(" <!-- ZRemover Halffacet cycle end -->");
	}
	PRINTD(" <!-- ZRemover Halffacet visit end -->");
}


#endif /* ENABLE_CGAL */

