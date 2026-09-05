//vimrun! ./osgslug-ammohud --clear-color 0.2,0.2,0.3,1.0

#include "osgslug-example.hpp"
#include "osgSlug/Font.hpp"

#include "slughorn/canvas.hpp"
#include "slughorn/serial.hpp"

#include <cmath>

static const std::string VERT_SHADER = R"(
#version 430 core

#pragma osgSlug lib_vertex

osgSlug_VertexResult osgSlug_Vertex(osgSlug_VertexData data) {
	if(data.effectId == 2) {
		data.pos.xy += data.effectParam * data.origin;
	}

	return osgSlug_VertexDefault(data);
}
)";

struct AmmoCallback : public osg::NodeCallback {
	static constexpr double REVEAL = 5.0;
	static constexpr double HOLD = 1.0;
	static constexpr double DURATION = REVEAL + HOLD;
	static constexpr double FADE = 0.15;
	static constexpr double EJECT = 0.4;
	static constexpr float DIM = 0.25f;

	const size_t N;
	osgSlug::Atlas* atlas;
	const size_t ejectBase;
	const size_t counterBase;

	AmmoCallback(size_t n, osgSlug::Atlas* a, size_t ejectBase_, size_t counterBase_):
	N(n),
	atlas(a),
	ejectBase(ejectBase_),
	counterBase(counterBase_) {
	}

	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		auto* sd = dynamic_cast<osgSlug::ShapeDrawable*>(node);
		const osg::FrameStamp* fs = nv->getFrameStamp();

		if(!sd || !fs) { traverse(node, nv); return; }

		const double t = std::fmod(fs->getSimulationTime(), DURATION);

		size_t ammo = static_cast<size_t>(N);
		float continuousAmmo = 0.0f;

		for(size_t i = 0; i < N; i++) {
			const double reveal = REVEAL * std::sqrt(cv(i) / (cv(N) - 1));
			const double fade = std::clamp((t - reveal) / FADE, 0.0, 1.0);
			const float bright = 1.0f - (1.0f - DIM) * cv(fade);

			if(t >= reveal + FADE) ammo--;

			continuousAmmo += 1.0f - cv(fade);

			sd->setLayerColor(i + 1, {1_cv, 1_cv, 1_cv, cv(bright)});

			const float ejectT = cv(std::clamp((t - reveal) / EJECT, 0.0, 1.0));

			sd->setLayerEffectParam(ejectBase + i, ejectT);
			sd->setLayerColor(ejectBase + i, {1_cv, 1_cv, 1_cv, 1_cv - ejectT});
		}

		sd->setLayerShapeIndex(counterBase, atlas->getShapeIndex(slughorn::Key(uint32_t('0' + ammo / 10))));
		sd->setLayerShapeIndex(counterBase + 1, atlas->getShapeIndex(slughorn::Key(uint32_t('0' + ammo % 10))));

		const float danger = 1.0f - continuousAmmo / static_cast<float>(N);
		sd->setLayerGradientTransform(N + 1, slughorn::Matrix{.xx=0_cv, .xy=1.25_cv, .dx=cv(0.5f - 1.5f * danger)});
		sd->setLayerColor(N + 1, {1_cv, 1_cv, 1_cv, cv(0.1f + danger * 0.9f)});

		sd->dirtyLayers();

		traverse(node, nv);
	}
};

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "TMP Demo Stuff! :)")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	slughorn::canvas::Canvas canvas(*atlas);

	// canvas.decomposer().tolerance = slughorn::TOLERANCE_FINE;

	const auto cx = 0.5_cv, cy = 0.5_cv;
	const auto rOuter = 0.4_cv, rInner = 0.32_cv;

	const auto seam = 0.06_cv;
	const auto seamCos = cv(std::cos(seam));
	const auto seamSin = cv(std::sin(seam));

	canvas.save();
	canvas.translate(cx, cy);
	canvas.rotate(-slughorn::PI_2_CV);
	canvas.translate(-cx, -cy);

	canvas.beginPath();
	canvas.moveTo(cx + rOuter * seamCos, cy + rOuter * seamSin);
	canvas.arc(cx, cy, rOuter, seam, slughorn::PI_CV - seam);
	canvas.lineTo(cx - rInner * seamCos, cy + rInner * seamSin);
	canvas.arc(cx, cy, rInner, slughorn::PI_CV - seam, seam, true);
	canvas.closePath();
	canvas.stroke(0.005_cv, {1_cv, 1_cv, 1_cv, 1_cv});

	const auto rMid = (rOuter + rInner) / 2_cv;
	const auto bW = 0.03_cv;
	const auto bH = 0.05_cv;
	const size_t N = 20;
	const auto margin = 0.1_cv;

	const auto tStart = -slughorn::PI_2_CV + margin + seam;
	const auto tStep = (slughorn::PI_CV - 2_cv * (margin + seam)) / cv(N - 1);

	// bullet: 1/4 end piece | gap | 1/2 body | gap | 1/4 triangle tip (outer)
	const auto gap      = bH / 12_cv;
	const auto pieceH   = (bH - 2_cv * gap) / 4_cv;
	const auto yBot     = rMid - bH / 2_cv;
	const auto yCaseTop = yBot + pieceH;
	const auto yBodyBot = yCaseTop + gap;
	const auto yBodyTop = yBodyBot + pieceH * 2_cv;
	const auto yTipBot  = yBodyTop + gap;
	const auto yTip     = yTipBot + pieceH;

	slughorn::canvas::Path bullet;

	bullet.rect(-bW / 2_cv, yBot, bW, pieceH);        // end piece
	bullet.rect(-bW / 2_cv, yBodyBot, bW, pieceH * 2_cv); // body
	bullet.moveTo(0_cv, yTip);                          // tip (triangle, outer)
	bullet.lineTo(-bW / 2_cv, yTipBot);
	bullet.lineTo(bW / 2_cv, yTipBot);
	bullet.closePath();

	for(size_t i = 0; i < N; i++) {
		const auto theta = tStart + cv(i) * tStep;

		canvas.save();
		canvas.translate(cx, cy);
		canvas.rotate(theta);
		canvas.beginPath();
		canvas.addPath(bullet, canvas.getTransform());
		canvas.fill({1_cv, 1_cv, 1_cv, 1_cv});
		canvas.restore();
	}

	// "Empty" half: same annular profile, opposite winding, static dim fill.
	canvas.beginPath();
	canvas.moveTo(cx + rOuter * seamCos, cy - rOuter * seamSin);
	canvas.arc(cx, cy, rOuter, -seam, seam - slughorn::PI_CV, true);
	canvas.lineTo(cx - rInner * seamCos, cy - rInner * seamSin);
	canvas.arc(cx, cy, rInner, seam - slughorn::PI_CV, -seam);
	canvas.closePath();

	auto emptyGrad = canvas.createLinearGradient(
		cx, cy + rOuter,
		cx, cy - rOuter,
		{
			{0.0_cv, {1_cv, 0.60_cv, 0.0_cv, 0.9_cv}},
			{0.5_cv, {0.9_cv, 0.20_cv, 0.0_cv, 0.6_cv}},
			{1.0_cv, {0.55_cv, 0.0_cv, 0.0_cv, 0.25_cv}}
		}
	);
	canvas.fillGradient(emptyGrad);

	canvas.restore();
	canvas.finalize("hud");

	// Eject bullet shapes — same geometry as bottom bullets, authored in the same CTM.
	// Custom origin stores the radial ejection direction (cos θ, sin θ) * ejectDist verbatim;
	// the vertex shader displaces pos.xy by effectParam * origin when effectId == 2.
	const auto ejectDist = 0.25_cv;

	canvas.save();
	canvas.translate(cx, cy);
	canvas.rotate(-slughorn::PI_2_CV);
	canvas.translate(-cx, -cy);

	for(size_t i = 0; i < N; i++) {
		const auto theta = tStart + cv(i) * tStep;

		canvas.save();
		canvas.translate(cx, cy);
		canvas.rotate(theta);
		canvas.beginPath();
		canvas.addPath(bullet, canvas.getTransform());
		canvas.fill(
			{1_cv, 1_cv, 1_cv, 1_cv}, 1_cv,
			slughorn::Atlas::ShapeInfo::Origin(
				slughorn::Atlas::ShapeInfo::Origin::Type::Custom,
				cv(std::cos(double(theta))) * ejectDist,
				cv(std::sin(double(theta))) * ejectDist
			)
		);
		canvas.restore();
	}

	canvas.restore();
	canvas.finalize("eject");

	auto font = osgx::make_ref<osgSlug::Font>("font/Orbitron-VariableFont_wght.ttf", atlas);

	slughorn::freetype::LoadConfig config;

	config.uniform = true;

	if(!font->load(&config)) return 1;

	atlas->build();
	atlas->packTextures();

	slughorn::serial::writeJSON(*atlas, std::cerr);

	canvas.text(
		"00",
		0.1_cv,
		cx, 0.33_cv,
		{1_cv, 1_cv, 1_cv, 1_cv},
		font->metrics(),
		slughorn::canvas::TextAnchorY::Baseline,
		slughorn::canvas::TextAlignX::Center
	);

	canvas.finalize("counter");

	auto sd = osgx::make_ref<osgSlug::ShapeDrawable>();

	sd->addCompositeShape(*atlas->getCompositeShape("hud")); // layers 0..N+1

	auto ejectComp = *atlas->getCompositeShape("eject"); // layers N+2..2N+1

	for(auto& layer : ejectComp.layers) layer.effectId = 2;

	sd->addCompositeShape(ejectComp);
	sd->addCompositeShape(*atlas->getCompositeShape("counter")); // layers 2N+2, 2N+3
	sd->setHooks({{osgSlug::Atlas::VertexHook, VERT_SHADER}});
	sd->setUpdateCallback(new AmmoCallback(N, atlas, N + 2, 2 * N + 2));

	atlas->addChild(sd);

	return example::run(viewer, args, atlas);
}
