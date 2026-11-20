#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Math/EditorGizmo.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace aegis;

namespace
{
	constexpr f32 c_ViewportWidth = 1280.0f;
	constexpr f32 c_ViewportHeight = 720.0f;

	glm::mat4 MakeViewProjection(const glm::vec3& camera, const glm::vec3& target)
	{
		const glm::mat4 view = glm::lookAt(camera, target, glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::mat4 projection =
			glm::perspective(glm::radians(60.0f), c_ViewportWidth / c_ViewportHeight, 0.1f, 100.0f);
		return projection * view;
	}
}

TEST_CASE("Gizmo: projecting world positions maps the view centre to the viewport centre")
{
	const glm::mat4 viewProjection = MakeViewProjection(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f));
	const glm::vec2 viewport(c_ViewportWidth, c_ViewportHeight);

	glm::vec2 centre(0.0f);
	REQUIRE(TranslationGizmo::Project(viewProjection, glm::vec3(0.0f), viewport, centre));
	CHECK(centre.x == doctest::Approx(c_ViewportWidth * 0.5f).epsilon(0.001));
	CHECK(centre.y == doctest::Approx(c_ViewportHeight * 0.5f).epsilon(0.001));

	// +X projects to the right and +Y projects upwards (smaller pixel y).
	glm::vec2 right(0.0f);
	REQUIRE(TranslationGizmo::Project(viewProjection, glm::vec3(1.0f, 0.0f, 0.0f), viewport, right));
	CHECK(right.x > centre.x);

	glm::vec2 up(0.0f);
	REQUIRE(TranslationGizmo::Project(viewProjection, glm::vec3(0.0f, 1.0f, 0.0f), viewport, up));
	CHECK(up.y < centre.y);

	// A point behind the camera cannot be projected.
	glm::vec2 behind(0.0f);
	CHECK_FALSE(TranslationGizmo::Project(viewProjection, glm::vec3(0.0f, 0.0f, 8.0f), viewport, behind));
}

TEST_CASE("Gizmo: handle length follows the camera distance")
{
	const glm::vec3 camera(0.0f, 0.0f, 5.0f);
	const glm::mat4 viewProjection = MakeViewProjection(camera, glm::vec3(0.0f));
	const glm::vec2 viewport(c_ViewportWidth, c_ViewportHeight);

	const TranslationGizmo near = TranslationGizmo::Build(viewProjection, glm::vec3(0.0f), camera, viewport);
	CHECK(near.GetAxisLength() == doctest::Approx(5.0f * 0.16f));
	CHECK(near.GetAxis(0).Visible);
	CHECK(near.GetAxis(1).Visible);
	CHECK(near.GetAxis(2).Visible);

	const TranslationGizmo far = TranslationGizmo::Build(viewProjection, glm::vec3(0.0f, 0.0f, 2.5f),
		glm::vec3(0.0f, 0.0f, 7.5f), viewport);
	CHECK(far.GetAxisLength() == doctest::Approx(5.0f * 0.16f));
}

TEST_CASE("Gizmo: picking hits the closest axis handle")
{
	const glm::vec3 camera(3.0f, 3.0f, 5.0f);
	const glm::mat4 viewProjection = MakeViewProjection(camera, glm::vec3(0.0f));
	const glm::vec2 viewport(c_ViewportWidth, c_ViewportHeight);
	const TranslationGizmo gizmo = TranslationGizmo::Build(viewProjection, glm::vec3(0.0f), camera, viewport);

	// The middle of the X handle must hit X and nothing else.
	const ScreenGizmoAxis& xAxis = gizmo.GetAxis(0);
	REQUIRE(xAxis.Visible);
	const glm::vec2 middle = (xAxis.ScreenOrigin + xAxis.ScreenEnd) * 0.5f;
	CHECK(gizmo.HitTestAxis(middle) == 0);

	const ScreenGizmoAxis& yAxis = gizmo.GetAxis(1);
	REQUIRE(yAxis.Visible);
	CHECK(gizmo.HitTestAxis((yAxis.ScreenOrigin + yAxis.ScreenEnd) * 0.5f) == 1);

	// Far away from every handle and from the centre.
	CHECK(gizmo.HitTestAxis(glm::vec2(5.0f, 5.0f)) == -1);

	// The centre handle is a small screen-space square around the origin.
	CHECK(gizmo.HitTestCenter(gizmo.GetScreenOrigin()));
	CHECK_FALSE(gizmo.HitTestCenter(gizmo.GetScreenOrigin() + glm::vec2(60.0f, 0.0f)));
}

TEST_CASE("Gizmo: drag deltas are constrained to one world axis")
{
	CHECK(TranslationGizmo::Constrain(glm::vec3(1.0f, 2.0f, 3.0f), 0) == glm::vec3(1.0f, 0.0f, 0.0f));
	CHECK(TranslationGizmo::Constrain(glm::vec3(1.0f, 2.0f, 3.0f), 1) == glm::vec3(0.0f, 2.0f, 0.0f));
	CHECK(TranslationGizmo::Constrain(glm::vec3(1.0f, 2.0f, 3.0f), 2) == glm::vec3(0.0f, 0.0f, 3.0f));
	CHECK(TranslationGizmo::Constrain(glm::vec3(1.0f, 2.0f, 3.0f), -1) == glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_CASE("Gizmo: ray and plane intersection drives the drag")
{
	const glm::vec3 camera(0.0f, 5.0f, 0.0f);
	const glm::vec3 target(0.0f, 0.0f, 0.0f);
	const std::optional<glm::vec3> hit = TranslationGizmo::IntersectPlane(camera, glm::vec3(0.0f, -1.0f, 0.0f),
		glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	REQUIRE(hit.has_value());
	CHECK(hit->x == doctest::Approx(0.0f));
	CHECK(hit->y == doctest::Approx(0.0f));
	CHECK(hit->z == doctest::Approx(0.0f));

	// A ray parallel to the plane never hits it, and a plane behind the ray is
	// rejected instead of producing a point behind the camera.
	CHECK_FALSE(TranslationGizmo::IntersectPlane(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)).has_value());
	CHECK_FALSE(TranslationGizmo::IntersectPlane(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
		glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)).has_value());

	// A drag point moves the object exactly along the chosen axis.
	const glm::vec3 start(0.0f);
	const std::optional<glm::vec3> grabbed = TranslationGizmo::IntersectPlane(glm::vec3(1.0f, 5.0f, 2.0f),
		glm::vec3(0.0f, -1.0f, 0.0f), start, glm::vec3(0.0f, 1.0f, 0.0f));
	REQUIRE(grabbed.has_value());
	const glm::vec3 delta = TranslationGizmo::Constrain(glm::vec3(2.5f, 0.0f, 1.5f), 0);
	CHECK(start + delta == glm::vec3(2.5f, 0.0f, 0.0f));
}
