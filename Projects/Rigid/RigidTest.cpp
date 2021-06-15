#include <zen/zen.h>
#include <zen/NumericObject.h>
#include <zen/PrimitiveObject.h>
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btShapeHull.h>
#include <memory>
#include <vector>


struct BulletCollisionShape : zen::IObject {
    std::unique_ptr<btCollisionShape> shape;

    BulletCollisionShape(std::unique_ptr<btCollisionShape> &&shape)
        : shape(std::move(shape)) {
    }
};

struct BulletMakeBoxShape : zen::INode {
    virtual void apply() override {
        auto v3size = get_input<zen::NumericObject>("v3size")->get<zen::vec3f>();
        auto shape = std::make_unique<BulletCollisionShape>(
            std::make_unique<btBoxShape>(zen::vec_to_other<btVector3>(v3size)));
        set_output("shape", std::move(shape));
    }
};

ZENDEFNODE(BulletMakeBoxShape, {
    {"v3size"},
    {"shape"},
    {},
    {"Rigid"},
});

struct BulletMakeSphereShape : zen::INode {
    virtual void apply() override {
        auto radius = get_input<zen::NumericObject>("radius")->get<float>();
        auto shape = std::make_unique<BulletCollisionShape>(
            std::make_unique<btSphereShape>(btScalar(radius)));
        set_output("shape", std::move(shape));
    }
};

ZENDEFNODE(BulletMakeSphereShape, {
    {"radius"},
    {"shape"},
    {},
    {"Rigid"},
});


struct BulletTriangleMesh : zen::IObject {
    btTriangleMesh mesh;
};

struct PrimitiveToBulletMesh : zen::INode {
    virtual void apply() override {
        auto prim = get_input<zen::PrimitiveObject>("prim");
        auto mesh = std::make_unique<BulletTriangleMesh>();
        auto pos = prim->attr<zen::vec3f>("pos");
        for (int i = 0; i < prim->tris.size(); i++) {
            auto f = prim->tris[i];
            mesh->mesh.addTriangle(
                zen::vec_to_other<btVector3>(pos[f[0]]),
                zen::vec_to_other<btVector3>(pos[f[1]]),
                zen::vec_to_other<btVector3>(pos[f[2]]));
        }
        set_output("mesh", std::move(mesh));
    }
};

ZENDEFNODE(PrimitiveToBulletMesh, {
    {"prim"},
    {"mesh"},
    {},
    {"Rigid"},
});

struct BulletMakeConvexHullShape : zen::INode {
    virtual void apply() override {

        auto triMesh = &get_input<BulletTriangleMesh>("triMesh")->mesh;
        auto inShape = std::make_unique<btConvexTriangleMeshShape>(triMesh);
        auto hull = std::make_unique<btShapeHull>(inShape.get());
        hull->buildHull(inShape->getMargin());

        auto shape = std::make_unique<BulletCollisionShape>(
            std::make_unique<btConvexHullShape>(
                reinterpret_cast<const float *>(hull->getVertexPointer()),
                hull->numVertices()));
        set_output("shape", std::move(shape));
    }
};

ZENDEFNODE(BulletMakeConvexHullShape, {
    {"triMesh"},
    {"shape"},
    {},
    {"Rigid"},
});


struct BulletTransform : zen::IObject {
    btTransform trans;
};

struct BulletMakeTransform : zen::INode {
    virtual void apply() override {
        auto origin = get_input<zen::NumericObject>("origin")->get<zen::vec3f>();
        auto trans = std::make_unique<BulletTransform>();
        trans->trans.setIdentity();
        trans->trans.setOrigin(btVector3(origin[0], origin[1], origin[2]));
        set_output("trans", std::move(trans));
    }
};

ZENDEFNODE(BulletMakeTransform, {
    {"origin"},
    {"trans"},
    {},
    {"Rigid"},
});


struct BulletObject : zen::IObject {
    std::unique_ptr<btDefaultMotionState> myMotionState;
    std::unique_ptr<btRigidBody> body;
    btScalar mass = 0.f;
    btTransform trans;

    BulletObject(btScalar mass_,
        btTransform const &trans,
        btCollisionShape *colShape)
        : mass(mass_)
    {
        btVector3 localInertia(0, 0, 0);
        if (mass != 0)
            colShape->calculateLocalInertia(mass, localInertia);

        //using motionstate is optional, it provides interpolation capabilities, and only synchronizes 'active' objects
        myMotionState = std::make_unique<btDefaultMotionState>(trans);
        btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, myMotionState.get(), colShape, localInertia);
        body = std::make_unique<btRigidBody>(rbInfo);
    }
};

struct BulletMakeObject : zen::INode {
    virtual void apply() override {
        auto shape = get_input<BulletCollisionShape>("shape");
        auto mass = get_input<zen::NumericObject>("mass")->get<float>();
        auto trans = get_input<BulletTransform>("trans");
        auto object = std::make_unique<BulletObject>(
            mass, trans->trans, shape->shape.get());
        set_output("object", std::move(object));
    }
};

ZENDEFNODE(BulletMakeObject, {
    {"shape", "trans", "mass"},
    {"object"},
    {},
    {"Rigid"},
});


struct BulletWorld : zen::IObject {

    std::unique_ptr<btDefaultCollisionConfiguration> collisionConfiguration = std::make_unique<btDefaultCollisionConfiguration>();
    std::unique_ptr<btCollisionDispatcher> dispatcher = std::make_unique<btCollisionDispatcher>(collisionConfiguration.get());
    std::unique_ptr<btBroadphaseInterface> overlappingPairCache = std::make_unique<btDbvtBroadphase>();
    std::unique_ptr<btSequentialImpulseConstraintSolver> solver = std::make_unique<btSequentialImpulseConstraintSolver>();

    std::unique_ptr<btDiscreteDynamicsWorld> dynamicsWorld = std::make_unique<btDiscreteDynamicsWorld>(dispatcher.get(), overlappingPairCache.get(), solver.get(), collisionConfiguration.get());

    std::vector<BulletObject *> objects;

    BulletWorld() {
        dynamicsWorld->setGravity(btVector3(0, -10, 0));
    }

    void addObject(BulletObject *obj) {
        dynamicsWorld->addRigidBody(obj->body.get());
        objects.push_back(obj);
    }

    /*
    void addGround() {
        auto groundShape = std::make_unique<btBoxShape>(btVector3(btScalar(50.), btScalar(50.), btScalar(50.)));

        btTransform groundTransform;
        groundTransform.setIdentity();
        groundTransform.setOrigin(btVector3(0, -56, 0));

        btScalar mass(0.);

        addObject(std::make_unique<BulletObject>(mass, groundTransform, std::move(groundShape)));
    }

    void addBall() {
        auto colShape = std::make_unique<btSphereShape>(btScalar(1.));

        btTransform startTransform;
        startTransform.setIdentity();

        btScalar mass(1.f);

        addObject(std::make_unique<BulletObject>(mass, startTransform, std::move(colShape)));
    }*/

    void step(float dt = 1.f / 60.f) {
        dynamicsWorld->stepSimulation(dt, 10);

        for (int j = dynamicsWorld->getNumCollisionObjects() - 1; j >= 0; j--)
        {
            btCollisionObject* obj = dynamicsWorld->getCollisionObjectArray()[j];
            btRigidBody* body = btRigidBody::upcast(obj);
            btTransform trans;
            if (body && body->getMotionState())
            {
                body->getMotionState()->getWorldTransform(trans);
            }
            else
            {
                trans = obj->getWorldTransform();
            }
            printf("world pos object %d = %f,%f,%f\n", j, float(trans.getOrigin().getX()), float(trans.getOrigin().getY()), float(trans.getOrigin().getZ()));
        }
    }
};

struct BulletMakeWorld : zen::INode {
    virtual void apply() override {
        auto world = std::make_unique<BulletWorld>();
        set_output("world", std::move(world));
    }
};

ZENDEFNODE(BulletMakeWorld, {
    {},
    {"world"},
    {},
    {"Rigid"},
});

struct BulletSetWorldGravity : zen::INode {
    virtual void apply() override {
        auto world = get_input<BulletWorld>("world");
        auto gravity = get_input<zen::NumericObject>("gravity")->get<zen::vec3f>();
        world->dynamicsWorld->setGravity(zen::vec_to_other<btVector3>(gravity));
    }
};

ZENDEFNODE(BulletSetWorldGravity, {
    {"world", "gravity"},
    {},
    {},
    {"Rigid"},
});

struct BulletStepWorld : zen::INode {
    virtual void apply() override {
        auto world = get_input<BulletWorld>("world");
        auto dt = get_input<zen::NumericObject>("dt")->get<float>();
        world->step(dt);
    }
};

ZENDEFNODE(BulletStepWorld, {
    {"world", "dt"},
    {},
    {},
    {"Rigid"},
});

struct BulletWorldAddObject : zen::INode {
    virtual void apply() override {
        auto world = get_input<BulletWorld>("world");
        auto object = get_input<BulletObject>("object");
        world->addObject(object);
        set_output_ref("world", get_input_ref("world"));
    }
};

ZENDEFNODE(BulletWorldAddObject, {
    {"world", "object"},
    {"world"},
    {},
    {"Rigid"},
});


#if 0
/// This is a Hello World program for running a basic Bullet physics simulation

int main(int argc, char** argv)
{
    ///-----initialization_start-----

    ///collision configuration contains default setup for memory, collision setup. Advanced users can create their own configuration.
    BulletWorld w;

    ///-----initialization_end-----

    //keep track of the shapes, we release memory at exit.
    //make sure to re-use collision shapes among rigid bodies whenever possible!
    w.addGround();
    w.addBall();

    ///create a few basic rigid bodies

    /// Do some simulation

    ///-----stepsimulation_start-----
    for (int i = 0; i < 120; i++) {
        w.step();
    }

    ///-----stepsimulation_end-----

    //cleanup in the reverse order of creation/initialization

    ///-----cleanup_start-----

    //remove the rigidbodies from the dynamics world and delete them


    return 0;
}
#endif
