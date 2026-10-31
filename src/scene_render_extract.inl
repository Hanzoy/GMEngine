// SceneRuntime::extract, after the 2D extraction. No GPU objects in the simulation.
rendering_.meshes.clear();
rendering_.cameraEnabled = false;
rendering_.camera = identity3D();
rendering_.lens = {};
rendering_.light = {};
rendering_.light.intensity = 0;
rendering_.light.ambient = .08f;
rendering_.post = {0};
impl_->context().each<const Impl::EntityIndex, const sdk::WorldTransform3D, const sdk::MeshRenderer>(
    [&](sdk::Entity, const Impl::EntityIndex &index, const sdk::WorldTransform3D &transform,
        const sdk::MeshRenderer &mesh) {
        if (!mesh.enabled || mesh.mesh == NoIndex)
            return;
        MeshDraw draw;
        draw.entity = index.value;
        draw.mesh = mesh.mesh;
        draw.material = mesh.material;
        std::copy_n(transform.matrix, 16, draw.transform.begin());
        rendering_.meshes.push_back(draw);
    });
int priority = INT32_MIN;
uint32_t cameraIndex = NoIndex;
impl_->context().each<const Impl::EntityIndex, const sdk::WorldTransform3D, const sdk::Camera3D>(
    [&](sdk::Entity, const Impl::EntityIndex &index, const sdk::WorldTransform3D &transform,
        const sdk::Camera3D &camera) {
        if (camera.enabled &&
            (camera.priority > priority || (camera.priority == priority && index.value < cameraIndex))) {
            rendering_.cameraEnabled = true;
            rendering_.lens = camera;
            std::copy_n(transform.matrix, 16, rendering_.camera.begin());
            priority = camera.priority;
            cameraIndex = index.value;
        }
    });
uint32_t lightIndex = NoIndex, postIndex = NoIndex;
impl_->context().each<const Impl::EntityIndex, const sdk::DirectionalLight>(
    [&](sdk::Entity, const Impl::EntityIndex &index, const sdk::DirectionalLight &light) {
        if (light.enabled && index.value < lightIndex) {
            rendering_.light = light;
            lightIndex = index.value;
        }
    });
impl_->context().each<const Impl::EntityIndex, const sdk::PostProcess>([&](sdk::Entity,
                                                                           const Impl::EntityIndex &index,
                                                                           const sdk::PostProcess &post) {
    if (post.enabled && index.value < postIndex) {
        rendering_.post = post;
        postIndex = index.value;
    }
});
