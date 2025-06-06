/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019-2023 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef RMLGUI_SVG_ELEMENT_SVG_H
#define RMLGUI_SVG_ELEMENT_SVG_H


#include "RmlUi/Core/CallbackTexture.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/Geometry.h"

namespace lunasvg {
class Document;
}

namespace RmlGui {

class ElementSVG : public Rml::Element {
public:
	RMLUI_RTTI_DefineWithParent(ElementSVG, Element)

	ElementSVG(const Rml::String& tag);
	virtual ~ElementSVG();

	/// Returns the element's inherent size.
	bool GetIntrinsicDimensions(Rml::Vector2f& dimensions, float& ratio) override;

protected:
	/// Renders the image.
	void OnRender() override;

	/// Regenerates the element's geometry.
	void OnResize() override;

	/// Checks for changes to the image's source or dimensions.
	/// @param[in] changed_attributes A list of attributes changed on the element.
	void OnAttributeChange(const Rml::ElementAttributes& changed_attributes) override;

	/// Called when properties on the element are changed.
	/// @param[in] changed_properties The properties changed on the element.
	void OnPropertyChange(const Rml::PropertyIdSet& changed_properties) override;

private:
	// Generates the element's geometry.
	void GenerateGeometry();
	// Loads the SVG document specified by the 'src' attribute.
	bool LoadSource();
	// Update the texture when necessary.
	void UpdateTexture();

	bool source_dirty = false;
	bool geometry_dirty = false;
	bool texture_dirty = false;

	// The texture this element is rendering from.
	Rml::CallbackTexture texture;

	// The image's intrinsic dimensions.
	Rml::Vector2f intrinsic_dimensions;
	// The element's size for rendering.
	Rml::Vector2i render_dimensions;

	// The geometry used to render this element.
	Rml::Geometry geometry;

	Rml::UniquePtr<lunasvg::Document> svg_document;
};

}

#endif
