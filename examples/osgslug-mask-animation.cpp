//vimrun! ./osgslug-mask-animation --clear-color 0.1,0.2,0.3

// Animated mask demo -- proves the "growing stroke / radial wipe / arc sweep" use case that
// motivated the procedural-vs-baked mask design split in the first place (see
// ai/context-todo-mask.md). Mutates a RenderMask's underlying slughorn::Mask params in place,
// frame to frame, then repack()s it -- no re-authoring, no atlas rebuild, no baking. Procedural
// mask types can animate their own *shape* this cheaply; an MSDF-baked mask can only cheaply
// animate its transform (translate/rotate/scale the sample coordinate), not its underlying
// geometry -- this demo exercises both.
//
// Scene is four quads, one per canvas quadrant, each with its own emoji glyph (COLR, loaded the
// same way as osgslug-emoji-grid.cpp) centered on the quad (see centerGlyphOnQuad() below) and
// its own distinct, independently animated Mask -- proves RenderGroup correctly splits/
// rebinds across several different non-null masks in one drawable, and that each mask's
// per-frame state is genuinely independent (not aliased/shared). Also doubles as the exercise
// demo for the newest procedural Mask::Types (Hexagon/Octagon/Star) plus a hand-drawn MSDF-baked
// heart -- the original six (MSDF/Circle/Rect/Capsule/Arc/ArcBand) stay demonstrated in the
// simpler, single-mask osgslug-mask.cpp instead of being duplicated here. A procedural Heart (two
// circles + a box wedge, composed via min()) was tried first and dropped: getting the wedge's
// corner to seat exactly in the lobes' cleft by hand turned out fragile, twice. A single authored
// vector path has no such seam to get wrong.
//
// Default per-quad emoji, deliberately rotated one slot away from a literal mask-shape match
// (e.g. red_heart under the heart mask): when a glyph's own silhouette nearly coincides with its
// mask, the reveal reads as a solid fill rather than a mask cropping something -- busier, non-
// matching glyphs make the masking obviously visible. Override with trailing EMOJI args.
//
// FONT_FILE [EMOJI ...]   a COLR emoji font (e.g. Twemoji/NotoColorEmoji) + optional emoji names
// --invert                knockout: discard inside the mask instead of outside

#include "osgslug-example.hpp"

#include "osgSlug/Font.hpp"

#include "slughorn/canvas.hpp"

#define SLUGHORN_EMOJI_IMPLEMENTATION
#include "slughorn/emoji.hpp"
#include "slughorn/serial.hpp"

#include <algorithm>
#include <array>
#include <cmath>

static constexpr float SWEEP_SPEED = 1.2f; // radians/sec-ish; just a pleasant pace
static constexpr float MSDF_RANGE = 0.025f; // matches osgslug-mask.cpp's MSDF mask

// Distance between adjacent quad centers. Hand-picked for now to clear the ~1em-wide glyphs at
// the new, bigger reveal radii below without neighbors bleeding into each other (confirmed via
// the atlas debug view, which draws every registered shape's raw extent unmasked) -- a real fix
// would derive this from each glyph's actual boundingBox() (see centerGlyphOnQuad) plus the
// largest mask radius in play, instead of a constant tuned by eye.
static constexpr float QUAD_SPACING = 1.4f;

// One center point per quad, in composite-add order (bottom-left, bottom-right, top-left,
// top-right) -- shared between scene construction and MaskAnimCallback's phase assignment.
static constexpr std::array<std::pair<float, float>, 4> QUAD_CENTERS = {{
	{-QUAD_SPACING * 0.5f, -QUAD_SPACING * 0.5f}, {QUAD_SPACING * 0.5f, -QUAD_SPACING * 0.5f},
	{-QUAD_SPACING * 0.5f, QUAD_SPACING * 0.5f}, {QUAD_SPACING * 0.5f, QUAD_SPACING * 0.5f}
}};

// Per-quad emoji content, in composite-add order (matches QUAD_CENTERS). Overridable via
// trailing EMOJI args on the command line.
static const std::array<std::string, 4> DEFAULT_EMOJIS = {
	"red_heart", "star", "octopus", "honeybee"
};

// Masking is fully automatic (osgSlug_FragmentMask, an always-linked early hook, evaluates and
// discards BEFORE slug_Render runs) -- no custom FragmentHook needed here at all anymore.
// Nothing about this demo is animation-specific on the GLSL side; all the animation work
// happens on the CPU side, see MaskAnimCallback below.

// ================================================================================================
// Scene construction helpers
// ================================================================================================

// Draws a heart outline into canvas's current path, apex down (matches canvas's Y-up
// convention -- lobes end up above the point, as expected). Local unit shape is hand-picked
// (four quadratic curves, mirrored left/right), scaled by r and centered at (cx, cy). Baked as
// an MSDF mask right after this call -- see main() below -- not evaluated as a closed-form SDF,
// so there's no seam/tangency to get wrong the way the dropped procedural version had.
static void heartPath(slughorn::canvas::Canvas& canvas, float cx, float cy, float r) {
	canvas.moveTo(cx, cy - r);
	canvas.quadTo(cx - r * 1.3f, cy - r * 0.3f, cx - r, cy + r * 0.35f);
	canvas.quadTo(cx - r * 0.8f, cy + r * 0.9f, cx, cy + r * 0.4f);
	canvas.quadTo(cx + r * 0.8f, cy + r * 0.9f, cx + r, cy + r * 0.35f);
	canvas.quadTo(cx + r * 1.3f, cy - r * 0.3f, cx, cy - r);
	canvas.closePath();
}

// A freshly loaded glyph's layers all sit at transform=(0,0)/scale=1 -- wherever FreeType's
// em-space decomposition happened to put them, unrelated to any particular quad. Translates every
// layer (rigidly, as one unit -- COLR layers must move together) so the glyph's own natural
// center lands exactly on (cx, cy).
//
// Deliberately does NOT touch layer.scale. osgSlug_FragmentMask (Atlas.shaders.cpp) computes
// canvasCoord = data.emCoord + layer.transform.xy using the shape's RAW, unscaled em-space
// coordinate -- layer.scale only ever reaches Shape::computeQuad() (the on-screen vertex quad),
// it never reaches emCoord/canvasCoord. A nonzero layer.scale therefore resizes the content on
// screen while the mask keeps evaluating in the original 1:1 coordinate space, silently
// misaligning the two (found the hard way: only the one quad where the scale factor happened to
// be near 1 masked correctly). Pure translation keeps content and mask in the same coordinate
// space, so a mask authored at this same (cx, cy) lines up correctly.
//
// MUST be called after atlas->build(): COLR glyphs are loaded with autoMetrics=true and no
// bearing/width/height set at load time (unlike canvas-authored shapes, whose metrics are
// computed immediately at commit time) -- those fields only become valid once build() has
// derived them from the raw curves.
static slughorn::CompositeShape centerGlyphOnQuad(
	const slughorn::CompositeShape& glyph,
	const osgSlug::Atlas& atlas,
	float cx, float cy
) {
	slughorn::CompositeShape out = glyph;
	const auto bbox = glyph.boundingBox(atlas);

	if(!bbox) return out; // no shape data resolved (shouldn't happen; caller already checked)

	const float midX0 = (bbox->x0 + bbox->x1) * 0.5f;
	const float midY0 = (bbox->y0 + bbox->y1) * 0.5f;

	for(auto& layer : out.layers) {
		layer.transform.x = cx - midX0;
		layer.transform.y = cy - midY0;
	}

	return out;
}

// ================================================================================================
// Animation helpers
// ================================================================================================

static float pingValue(double t, float phase) {
	return 0.5f + 0.5f * static_cast<float>(std::sin(t * SWEEP_SPEED + phase));
}

// Mutates m's "growing"/"spinning"/"zooming" field(s) in place, branching on m.type -- shared by
// every mask this demo animates. cx/cy of each mask's own fixed anchor is already baked into an
// untouched params[] slot from construction (hexagon's/octagon's/star's own center, MSDF's
// baked-in cx/cy).
static void animateMask(slughorn::Mask& m, double t, float phase) {
	switch(m.type) {
	case slughorn::Mask::Type::Hexagon: {
		// Pulse the radius (params: cx, cy, r, rotation -- rotation stays at its fixed
		// construction-time value).
		static constexpr float MIN_R = 0.18f, MAX_R = 0.45f;
		const float ping = pingValue(t, phase);

		m.params[2] = MIN_R + ping * (MAX_R - MIN_R);
		break;
	}

	case slughorn::Mask::Type::Octagon: {
		// Continuous spin, not a ping-pong -- uses t directly (radius stays fixed).
		static constexpr float SPIN_SPEED = 0.8f;

		m.params[3] = static_cast<float>(t) * SPIN_SPEED + phase; // rotation
		break;
	}

	case slughorn::Mask::Type::MSDF: {
		// The heart quad: a hand-drawn vector path baked into an MSDF tile at authoring time
		// (see main() below) -- its geometry can't cheaply re-animate (that would mean
		// re-baking), but its SAMPLING WINDOW (params: cx, cy, r, range) can, which is exactly
		// the "cheaply transform-animatable" property baked masks have. osgSlug_Mask_CoverageFor
		// (Atlas.shaders.cpp) hard-clamps to maskFill=0 outside [cx-r-range, cx+r+range] in
		// canvas space, so r IS the reveal's on-screen half-extent -- bigger r = bigger apparent
		// heart, same direction as every other type here. (A previous version of this comment
		// claimed the opposite; that was wrong, caught when the reveal stopped growing as r
		// shrank instead of growing.) range (params[3]) is left untouched; it's a small,
		// roughly-fixed AA padding, not a size.
		static constexpr float MIN_R = 0.18f, MAX_R = 0.42f;
		const float ping = pingValue(t, phase);

		m.params[2] = MIN_R + ping * (MAX_R - MIN_R);
		break;
	}

	case slughorn::Mask::Type::Star: {
		// Morph inner_ratio between sharp spikes and a near-regular polygon (params2.x --
		// r/points/rotation stay at their fixed construction-time values).
		static constexpr float MIN_RATIO = 0.25f, MAX_RATIO = 0.75f;
		const float ping = pingValue(t, phase);

		m.params[4] = MIN_RATIO + ping * (MAX_RATIO - MIN_RATIO); // inner_ratio
		break;
	}

	default:
		break;
	}
}

// ================================================================================================
// MaskAnimCallback - mutates each RenderMask's slughorn::Mask in place every frame, then repacks
// it. This is the whole point of the demo: no atlas rebuild, no re-baking, just a few floats
// changing and one small UBO re-upload per mask. Attached directly to the ShapeDrawable,
// mirroring osgslug-ssbo.cpp's ColorCallback (cast node, pull osg::FrameStamp for time).
// ================================================================================================

struct MaskAnimCallback: public osg::NodeCallback {
	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		auto* sd = dynamic_cast<osgSlug::ShapeDrawable*>(node);
		const osg::FrameStamp* fs = nv->getFrameStamp();

		if(!sd || !fs) { traverse(node, nv); return; }

		auto* atlas = sd->getAtlas();

		if(atlas) {
			const double t = fs->getSimulationTime();

			// One independently-authored mask per quad -- phase-offset per quad (a quarter turn
			// apart) purely so it's visually obvious each RenderMask is keeping its own state,
			// not reading/writing a shared one. Can't assume one layer per quad: an emoji glyph's
			// COLR layers all share one RenderMask pointer (see RenderShape::mask in
			// ShapeDrawable.hpp), so walk every layer but animate + repack each DISTINCT mask
			// pointer only once, assigning phases in first-seen order.
			const size_t layerCount = sd->getLayers().size();
			std::vector<osgSlug::RenderMask*> seen;

			for(size_t i = 0; i < layerCount; ++i) {
				auto* mask = sd->getLayerMask(i);

				if(!mask || std::ranges::find(seen, mask) != seen.end()) continue;

				const float phase = static_cast<float>(seen.size()) * slughorn::PI_2_CV;

				animateMask(mask->mask(), t, phase);
				mask->repack(*atlas);

				seen.push_back(mask);
			}
		}

		traverse(node, nv);
	}
};

// ================================================================================================
// main
// ================================================================================================

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);
	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(
		args,
		"Four emoji glyphs, each masked by its own independently animated mask",
		{},
		1,
		"FONT_FILE [EMOJI ...]"
	)) return 0;

	bool invert = false;

	if(args.read("--invert")) invert = true;

	const std::string fontFile = args[1];
	std::vector<std::string> emojiNames;

	// for(int i = 2; i < args.argc(); i++) emojiNames.push_back(args[i]);

	while(emojiNames.size() < DEFAULT_EMOJIS.size()) {
		emojiNames.push_back(DEFAULT_EMOJIS[emojiNames.size()]);
	}

	std::vector<uint32_t> codepoints;

	for(const auto& name : emojiNames) {
		auto cp = slughorn::emoji::nameToCodepoint(name);

		if(!cp) {
			OSG_FATAL << "osgslug-mask-animation: unknown emoji name '" << name << "'" << std::endl;

			return example::fail(args, 2);
		}

		codepoints.push_back(*cp);
	}

	auto atlas = osgx::make_ref<osgSlug::Atlas>();
	slughorn::canvas::Canvas canvas(*atlas);
	auto font = osgx::make_ref<osgSlug::Font>(atlas);

	if(!font->loadEmoji(fontFile, codepoints)) {
		OSG_WARN << "Some emoji may be missing from the font" << std::endl;
	}

	// Heart mask: hand-drawn path baked as an MSDF tile (no closed-form SDF for a heart -- see
	// heartPath() above). Registration (like requestMSDF() generally) is safe pre-build; capture
	// the resulting Mask now and attach it to its quad's composite once glyph metrics are
	// available below.
	canvas.beginPath();
	heartPath(canvas, QUAD_CENTERS[2].first, QUAD_CENTERS[2].second, 0.35f);
	const slughorn::Mask heartMask = canvas.mask(MSDF_RANGE, invert);
	canvas.finalize(); // discards canvas's own (layer-less) staged composite

	// Freezes the atlas. Must happen before centerGlyphOnQuad() below -- see its doc comment.
	atlas->setMSDFTileSize(128);
	atlas->build();
	atlas->packTextures();

	std::vector<slughorn::CompositeShape> composites;

	// Centers codepoints[i]'s glyph on QUAD_CENTERS[i] and attaches mask to the result; warns
	// and skips (rather than crashing) if the font had no COLR data for that codepoint.
	auto addEmojiQuad = [&](size_t i, slughorn::Mask mask) {
		const auto* glyph = font->getColorGlyph(codepoints[i]);

		if(!glyph || glyph->layers.empty()) {
			OSG_WARN << "No COLR data for '" << emojiNames[i] << "'" << std::endl;

			return;
		}

		auto composite = centerGlyphOnQuad(*glyph, *atlas, QUAD_CENTERS[i].first, QUAD_CENTERS[i].second);

		composite.mask = mask;

		composites.push_back(std::move(composite));
	};

	addEmojiQuad(0, slughorn::Mask::hexagon(
		QUAD_CENTERS[0].first, QUAD_CENTERS[0].second, 0.3f, 0.3f, invert));
	addEmojiQuad(1, slughorn::Mask::octagon(
		QUAD_CENTERS[1].first, QUAD_CENTERS[1].second, 0.4f, 0.0f, invert));
	addEmojiQuad(2, heartMask);
	addEmojiQuad(3, slughorn::Mask::star(
		QUAD_CENTERS[3].first, QUAD_CENTERS[3].second, 0.4f, 5.0f, 0.5f, 0.0f, invert));

	auto sd = example::makeShapeDrawable();

	for(const auto& composite : composites) sd->addCompositeShape(composite);

	sd->setUpdateCallback(new MaskAnimCallback());

	// No StateSet override: sd inherits the Atlas's own default StateSet, which already links
	// the automatic masking hook -- see osgslug-mask.cpp's equivalent comment.
	atlas->addChild(sd);

	return example::run(viewer, args, atlas);
}
