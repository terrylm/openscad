#pragma once

#include "renderer.h"
#include "CGAL_Nef_polyhedron.h"

class CGALRenderer : public Renderer
{
public:
	CGALRenderer(shared_ptr<const class Geometry> geom);
	~CGALRenderer();
	virtual void draw(bool showfaces, bool showedges) const;
	virtual void setColorScheme(const OSColors::colorscheme &cs);

	void rebuildPolyhedron();
public:
	class Polyhedron *polyhedron;
	shared_ptr<const CGAL_Nef_polyhedron3> nef3;
	shared_ptr<const class PolySet> polyset;
};
