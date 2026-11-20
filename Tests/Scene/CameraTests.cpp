#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Scene/Camera.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace aegis;

TEST_CASE("Camera: view matrix looks down -Z for an identity transform")
{
	TransformComponent transform;
	transform.WorldPosition = glm::vec3(0.0f);
	transform.WorldRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

	const glm::mat4 view = Camera::ComputeView(transform);
	const glm::vec4 forward = view * glm::vec4(0.0f, 0.0f, -5.0f, 0.0f);
	CHECK(forward.z == doctest::Approx(-5.0f));
	CHECK(forward.x == doctest::Approx(0.0f));

	const glm::vec4 origin = view * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	CHECK(origin.z == doctest::Approx(0.0f));
}

TEST_CASE("Camera: perspective projection maps near/far to Vulkan depth range")
{
	CameraComponent camera;
	camera.FieldOfView = 60.0f;
	camera.NearClip = 0.1f;
	camera.FarClip = 100.0f;

	Camera viewport;
	viewport.SetViewportSize(1920, 1080);

	const glm::mat4 projection = glm::perspective(glm::radians(camera.FieldOfView), viewport.GetAspectRatio(),
		camera.NearClip, camera.FarClip);

	TransformComponent transform;
	const glm::mat4 viewProjection = viewport.ComputeViewProjection(transform, camera);
	// The renderer consumes the projection unchanged; the depth range is [0, 1].
	CHECK(viewProjection[1][1] == doctest::Approx(projection[1][1]));

	const glm::mat4 zeroToOne = Camera::ComputeProjection(camera, viewport.GetAspectRatio());
	const glm::vec4 nearPoint = zeroToOne * glm::vec4(0.0f, 0.0f, -camera.NearClip, 1.0f);
	const glm::vec4 farPoint = zeroToOne * glm::vec4(0.0f, 0.0f, -camera.FarClip, 1.0f);
	CHECK(nearPoint.z / nearPoint.w == doctest::Approx(0.0f).epsilon(0.001));
	CHECK(farPoint.z / farPoint.w == doctest::Approx(1.0f).epsilon(0.001));
}

TEST_CASE("Camera: orthographic projection matches the configured size")
{
	CameraComponent camera;
	camera.Projection = CameraProjection::Orthographic;
	camera.OrthographicSize = 5.0f;
	camera.NearClip = -10.0f;
	camera.FarClip = 10.0f;

	const glm::mat4 projection = Camera::ComputeProjection(camera, 2.0f);
	// Left/right = +-10 (size * aspect), bottom/top = +-5.
	CHECK(projection[0][0] == doctest::Approx(0.1f));
	CHECK(projection[1][1] == doctest::Approx(0.2f));
}

TEST_CASE("Camera: ray from the viewport centre points forward")
{
	CameraComponent camera;
	camera.FieldOfView = 60.0f;

	TransformComponent transform;
	transform.WorldPosition = glm::vec3(0.0f, 1.0f, 3.0f);

	glm::vec3 origin;
	glm::vec3 direction;
	Camera::ComputeRay(transform, camera, 16.0f / 9.0f, glm::vec2(0.5f, 0.5f), origin, direction);

	CHECK(origin.x == doctest::Approx(0.0f));
	CHECK(origin.z == doctest::Approx(3.0f));
	CHECK(direction.z == doctest::Approx(-1.0f).epsilon(0.001));
}

TEST_CASE("Camera: world to viewport round trips through the view projection")
{
	Camera viewport;
	viewport.SetViewportSize(800, 600);

	CameraComponent camera;
	camera.FieldOfView = 45.0f;
	camera.NearClip = 0.1f;
	camera.FarClip = 100.0f;

	TransformComponent transform;
	transform.WorldPosition = glm::vec3(0.0f, 0.0f, 5.0f);
	transform.WorldRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

	const glm::mat4 viewProjection = viewport.ComputeViewProjection(transform, camera);
	const glm::vec3 viewportPosition = Camera::WorldToViewport(viewProjection, glm::vec3(0.0f));

	CHECK(viewportPosition.x == doctest::Approx(0.5f));
	CHECK(viewportPosition.y == doctest::Approx(0.5f));
}

TEST_CASE("Camera: euler angle helper round trips")
{
	TransformComponent transform;
	transform.SetLocalEulerAngles(glm::vec3(15.0f, 45.0f, -30.0f));
	const glm::vec3 euler = transform.GetLocalEulerAngles();
	CHECK(euler.y == doctest::Approx(45.0f).epsilon(0.01));
}
