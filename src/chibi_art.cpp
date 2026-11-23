#include "chibi_art.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace mario {
namespace {

constexpr int SS = 4;
constexpr float Pi = 3.14159265358979323846f;
struct Point { float x, y; };
struct Color { unsigned char r, g, b; };

Color color(const std::array<unsigned char, 3>& value) {
    return {value[0], value[1], value[2]};
}
Color mix(Color a, Color b, float amount) {
    const auto channel = [amount](unsigned char x, unsigned char y) {
        return static_cast<unsigned char>(std::clamp(
            std::lround(x + (y - x) * amount), 0L, 255L));
    };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)};
}
Color light(Color a, float amount) { return mix(a, {255, 246, 234}, amount); }
Color dark(Color a, float amount) { return mix(a, {31, 23, 50}, amount); }

// The painter operates on curves in a 64x128 logical canvas. The final raster
// averages a 4x coverage mask, rather than enlarging the native pixel silhouette.
class Painter {
public:
    explicit Painter(bool small, bool grow)
        : small_(small), grow_(grow),
          samples_(ChibiWidth * SS * ChibiHeight * SS * 4, 0) {}

    void local(float dx, float dy, float angle = 0,
               Point pivot = {32.0f, 34.0f}, float scale = 1.0f) {
        dx_ = dx; dy_ = dy; angle_ = angle * Pi / 180.0f; pivot_ = pivot; scale_ = scale;
    }
    void resetLocal() { local(0, 0); }

    Point project(Point p) const {
        const float cs = std::cos(angle_), sn = std::sin(angle_);
        const float x = (p.x - pivot_.x)*scale_, y = (p.y - pivot_.y)*scale_;
        p = {pivot_.x + x * cs - y * sn + dx_,
             pivot_.y + x * sn + y * cs + dy_};
        if (small_) {
            // A compact anatomical rig is drawn at one uniform scale. The
            // face, badge, fingers and shoes therefore retain their shape.
            p.x = 32.0f + (p.x - 32.0f) * 0.72f;
            p.y = 64.0f + (p.y - 3.0f) * 0.72f;
        } else if (grow_) {
            p.x = 32.0f + (p.x - 32.0f) * 0.75f;
            p.y = 32.0f + p.y * 0.75f;
        }
        return p;
    }

    void polygon(const std::vector<Point>& points, Color fill, float alpha = 1) {
        if (points.size() < 3) return;
        std::vector<Point> poly;
        poly.reserve(points.size());
        float xmin = 64, xmax = 0, ymin = 128, ymax = 0;
        for (Point point : points) {
            point = project(point);
            xmin = std::min(xmin, point.x); xmax = std::max(xmax, point.x);
            ymin = std::min(ymin, point.y); ymax = std::max(ymax, point.y);
            poly.push_back(point);
        }
        const int left = std::clamp(int(std::floor(xmin * SS)), 0, ChibiWidth * SS);
        const int right = std::clamp(int(std::ceil(xmax * SS)), 0, ChibiWidth * SS);
        const int top = std::clamp(int(std::floor(ymin * SS)), 0, ChibiHeight * SS);
        const int bottom = std::clamp(int(std::ceil(ymax * SS)), 0, ChibiHeight * SS);
        // Calculate each curve's intersections once per scanline. Artwork is
        // created only on palette/pose cache misses, and this avoids a costly
        // point-in-polygon walk for every supersampled pixel.
        std::vector<float> intersections;
        intersections.reserve(poly.size());
        for (int yy = top; yy < bottom; ++yy) {
            const float y = (yy + 0.5f) / SS;
            intersections.clear();
            for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
                const Point a = poly[i], b = poly[j];
                if ((a.y > y) != (b.y > y))
                    intersections.push_back((b.x-a.x)*(y-a.y)/(b.y-a.y)+a.x);
            }
            std::sort(intersections.begin(),intersections.end());
            for (std::size_t i = 0; i+1 < intersections.size(); i += 2) {
                const int start=std::clamp(int(std::ceil(intersections[i]*SS-0.5f)),left,right);
                const int stop=std::clamp(int(std::ceil(intersections[i+1]*SS-0.5f)),left,right);
                for (int xx=start; xx<stop; ++xx) pixel(xx,yy,fill,alpha);
            }
        }
    }
    void polygon(std::initializer_list<Point> points, Color fill, float alpha = 1) {
        polygon(std::vector<Point>(points), fill, alpha);
    }
    void ellipse(float x, float y, float rx, float ry, Color fill,
                 float alpha = 1) {
        std::vector<Point> points;
        points.reserve(48);
        for (int i = 0; i < 48; ++i) {
            const float a = float(i) * (2 * Pi / 48);
            points.push_back({x + std::cos(a) * rx, y + std::sin(a) * ry});
        }
        polygon(points, fill, alpha);
    }
    void line(Point a, Point b, float width, Color fill, float alpha = 1) {
        const float len = std::hypot(b.x - a.x, b.y - a.y);
        if (len < 0.0001f) { ellipse(a.x, a.y, width / 2, width / 2, fill, alpha); return; }
        const Point n{-(b.y - a.y) * width / (2 * len),
                       (b.x - a.x) * width / (2 * len)};
        polygon({{a.x+n.x,a.y+n.y},{b.x+n.x,b.y+n.y},
                 {b.x-n.x,b.y-n.y},{a.x-n.x,a.y-n.y}}, fill, alpha);
        ellipse(a.x, a.y, width / 2, width / 2, fill, alpha);
        ellipse(b.x, b.y, width / 2, width / 2, fill, alpha);
    }
    void outline(const std::vector<Point>& points, Color fill, Color edge,
                 float thickness = 1.3f) {
        for (std::size_t i = 0; i < points.size(); ++i)
            line(points[i], points[(i + 1) % points.size()], thickness * 2, edge);
        polygon(points, fill);
    }
    void outlineEllipse(float x, float y, float rx, float ry, Color fill,
                        Color edge, float thickness = 1.2f) {
        ellipse(x, y, rx + thickness, ry + thickness, edge);
        ellipse(x, y, rx, ry, fill);
    }
    void capsule(Point a, Point b, float width, Color fill, Color edge,
                 float thickness = 1.2f) {
        line(a, b, width + thickness * 2, edge);
        line(a, b, width, fill);
    }
    void curve(Point a, Point b, Point c, Point d, float width, Color fill,
               float alpha = 1) {
        Point prev = a;
        for (int i = 1; i <= 20; ++i) {
            const float t = i / 20.0f, u = 1 - t;
            Point next{u*u*u*a.x + 3*u*u*t*b.x + 3*u*t*t*c.x + t*t*t*d.x,
                       u*u*u*a.y + 3*u*u*t*b.y + 3*u*t*t*c.y + t*t*t*d.y};
            line(prev, next, width, fill, alpha); prev = next;
        }
    }
    void finish(std::vector<unsigned char>& out) const {
        out.assign(ChibiWidth * ChibiHeight * 4, 0);
        const int firstRow = small_ ? 64 : (grow_ ? 32 : 0);
        for (int y = firstRow; y < ChibiHeight; ++y) {
            for (int x = 0; x < ChibiWidth; ++x) {
                std::uint32_t rr = 0, gg = 0, bb = 0, coverage = 0;
                for (int sy = 0; sy < SS; ++sy) for (int sx = 0; sx < SS; ++sx) {
                    const std::size_t at = std::size_t((y*SS+sy)*(ChibiWidth*SS)+x*SS+sx)*4;
                    const unsigned alpha = samples_[at+3];
                    rr += samples_[at] * alpha; gg += samples_[at+1] * alpha;
                    bb += samples_[at+2] * alpha; coverage += alpha;
                }
                const std::size_t at = std::size_t(y * ChibiWidth + x) * 4;
                if (coverage) {
                    out[at] = static_cast<unsigned char>((rr + coverage / 2) / coverage);
                    out[at+1] = static_cast<unsigned char>((gg + coverage / 2) / coverage);
                    out[at+2] = static_cast<unsigned char>((bb + coverage / 2) / coverage);
                    out[at+3] = static_cast<unsigned char>((coverage + SS*SS/2) / (SS*SS));
                }
            }
        }
    }
private:
    void pixel(int x, int y, Color fill, float amount) {
        const std::size_t at = std::size_t(y * (ChibiWidth * SS) + x) * 4;
        const float oldAlpha = samples_[at+3] / 255.0f;
        const float alpha = amount + oldAlpha * (1 - amount);
        if (alpha <= 0) return;
        const auto blend = [&](unsigned char previous, unsigned char next) {
            return static_cast<unsigned char>(std::clamp(std::lround(
                (next * amount + previous * oldAlpha * (1-amount)) / alpha),
                0L, 255L));
        };
        samples_[at] = blend(samples_[at], fill.r);
        samples_[at+1] = blend(samples_[at+1], fill.g);
        samples_[at+2] = blend(samples_[at+2], fill.b);
        samples_[at+3] = static_cast<unsigned char>(std::lround(alpha * 255));
    }
    bool small_, grow_;
    float dx_ = 0, dy_ = 0, angle_ = 0, scale_ = 1;
    Point pivot_{32,34};
    std::vector<unsigned char> samples_;
};


enum class Gesture { Relaxed, Fist, Open, Grip, Throw };

struct Rig {
    bool small = false;
    float bodyX = 0, headX = 0, headY = 0, headAngle = 0;
    float shoulderY = 74, hipY = 94;
    float thigh = 13.0f, shin = 14.0f, upperArm = 10.0f, forearm = 12.0f;
    Point rearKnee{27,109}, rearAnkle{23,120};
    Point frontKnee{37,110}, frontAnkle{40,120};
    Point rearElbow{17,83}, rearHand{17,94};
    Point frontElbow{48,84}, frontHand{48,95};
    float frontShoeAngle = 0, rearShoeAngle = 0;
    // -1 bends an elbow like a right-facing arm seen from the side: the
    // forearm folds forwards. +1 suits frontal poses with elbows pointing out.
    int rearArmBend = -1, frontArmBend = -1;
    Gesture rearGesture = Gesture::Relaxed, frontGesture = Gesture::Relaxed;
    bool crouch = false, death = false, skid = false, climb = false, swim = false;
    bool throwBall = false;
};

float normalizedPhase(float phase) {
    if (!std::isfinite(phase)) return 0;
    phase -= std::floor(phase);
    return phase < 0 ? phase + 1 : phase;
}
float lerp(float a,float b,float t) { return a+(b-a)*t; }
float smooth(float t) { return t*t*(3-2*t); }
Point offset(Point point,float x,float y) { return {point.x+x,point.y+y}; }

// Inverse kinematics preserves the length of both bones. If a requested target
// lies outside the arm's reach, the hand itself is clamped along that direction.
void solveLimb(Point root,Point& target,float first,float second,int bend,
               Point& joint) {
    float dx=target.x-root.x,dy=target.y-root.y;
    float distance=std::hypot(dx,dy);
    if(distance<0.001f) {dx=0;dy=1;distance=1;}
    const float ux=dx/distance,uy=dy/distance;
    const float clamped=std::clamp(distance,std::abs(first-second)+0.05f,
                                  first+second-0.05f);
    target={root.x+ux*clamped,root.y+uy*clamped};
    const float along=(first*first-second*second+clamped*clamped)/(2*clamped);
    const float height=std::sqrt(std::max(0.0f,first*first-along*along));
    joint={root.x+ux*along+uy*height*bend,
           root.y+uy*along-ux*height*bend};
}

int actionFor(int pose) {
    if(pose>=12 && pose<=21) return pose-12;
    if(pose==23 || pose==24 || pose==25) return 25;
    return pose;
}
float keyPhase(int action) {
    if(action>=0 && action<=2) return action/3.0f;
    if(action>=5 && action<=7) return (action-5)/3.0f;
    if(action>=8 && action<=9) return (action-8)/2.0f;
    return 0;
}
Point lerp(Point a,Point b,float t) { return {lerp(a.x,b.x,t),lerp(a.y,b.y,t)}; }
// Leg angles use the same convention as arms: zero hangs straight down and
// positive swings forwards. Flex folds the shin back, keeping the knee forward.
Point legReach(Point hip,float angle,float flex,float thigh,float shin) {
    return {hip.x+thigh*std::sin(angle)+shin*std::sin(angle-flex),
            hip.y+thigh*std::cos(angle)+shin*std::cos(angle-flex)};
}
void legAngles(Point hip,Point ankle,float thigh,float shin,float& angle,float& flex) {
    const float dx=ankle.x-hip.x,dy=ankle.y-hip.y;
    const float distance=std::clamp(std::hypot(dx,dy),std::abs(thigh-shin)+0.05f,
                                    thigh+shin-0.05f);
    const float spread=std::acos(std::clamp(
        (thigh*thigh+distance*distance-shin*shin)/(2*thigh*distance),-1.0f,1.0f));
    const float knee=std::acos(std::clamp(
        (thigh*thigh+shin*shin-distance*distance)/(2*thigh*shin),-1.0f,1.0f));
    angle=std::atan2(dx,dy)+spread;flex=Pi-knee;
}
// The planted foot slides back along the floor inside the leg's reach. The
// swinging leg is driven by its joints: the knee lifts forwards and folds so
// the foot passes under the hip, then the shin unfolds for the next contact.
// Both contact poses are solved at the same hip height, so the loop is closed.
// A walk keeps the swinging foot low; a run lifts the knee high.
Point walkFoot(float phase,Point hip,float contactHipY,float floorY,float stride,
               float thigh,float shin,float gait) {
    phase=normalizedPhase(phase);
    if(phase<0.5f) return {hip.x+stride*(1-4*phase),floorY};
    const float swing=(phase-0.5f)*2;
    float back,backFlex,front,frontFlex;
    legAngles({hip.x,contactHipY},{hip.x-stride,floorY},thigh,shin,back,backFlex);
    legAngles({hip.x,contactHipY},{hip.x+stride,floorY},thigh,shin,front,frontFlex);
    const float arc=std::sin(Pi*swing);
    const float lead=1-(1-swing)*(1-swing);
    const float angle=lerp(back,front,lead)+lerp(0.12f,0.38f,gait)*arc;
    const float flex=lerp(backFlex,frontFlex,swing)+lerp(0.75f,1.36f,gait)*arc;
    Point ankle=legReach(hip,angle,flex,thigh,shin);
    ankle.y=std::min(ankle.y,floorY);
    return ankle;
}
// Heel strike raises the toe, push-off points it down; the swinging foot
// trails toe-down behind the folded knee and levels out before contact.
float walkingShoe(float phase,float gait) {
    phase=normalizedPhase(phase);
    if(phase<0.5f) return lerp(-10.0f,16.0f,smooth(phase*2));
    const float swing=(phase-0.5f)*2;
    return lerp(16.0f,-10.0f,smooth(swing))+
        lerp(10.0f,22.0f,gait)*std::sin(Pi*std::pow(swing,0.7f));
}
// Forward kinematics of a side-view arm. Zero hangs straight down, positive
// swing moves the arm towards the facing direction and flex folds the forearm
// forwards. The resulting hand lies on the arm's -1 bend solution.
Point armReach(Point shoulder,float swing,float flex,float upper,float fore) {
    const float a=swing*Pi/180,b=(swing+flex)*Pi/180;
    return {shoulder.x+upper*std::sin(a)+fore*std::sin(b),
            shoulder.y+upper*std::cos(a)+fore*std::cos(b)};
}

Rig rigForMotion(const ChibiMotion& motion) {
    const int native=std::clamp(motion.nativePose,0,25);
    const int action=actionFor(native);
    const bool animated=motion.phase>=0 && std::isfinite(motion.phase);
    const float phase=animated?normalizedPhase(motion.phase):keyPhase(action);
    const float cycle=phase*2*Pi;
    const float vertical=std::isfinite(motion.verticalSpeed)?motion.verticalSpeed:0.0f;
    const float rising=std::clamp((-vertical)/5.0f,0.0f,1.0f);
    const float falling=std::clamp(vertical/5.0f,0.0f,1.0f);
    Rig r;
    r.small=native>=12 && native<=23;
    if(r.small) {
        r.shoulderY=59; r.hipY=68.2f;
        r.thigh=8.0f; r.shin=9.0f;
        r.upperArm=7.4f; r.forearm=9.0f;
        r.rearAnkle={26,85};r.frontAnkle={37,85};
        r.rearHand={17,74};r.frontHand={47,74};
    }
    const float floorY=r.small?85.0f:119.6f;
    const float shoulder=r.shoulderY;
    const float hip=r.hipY;

    // Vertical offsets authored for the large rig shrink with the small legs.
    const float k=r.small?0.62f:1.0f;
    if(action>=0 && action<=2) {
        // Legs apart at contact carry the body lowest; it rises while one
        // leg passes under the hips. Both feet stay within straight reach.
        const float gait=std::isfinite(motion.gait)?std::clamp(motion.gait,0.0f,1.0f):0.0f;
        const float amplitude=(r.small?0.7f:1.0f)*lerp(0.6f,1.25f,gait);
        const float bob=amplitude*std::cos(cycle*2);
        const float baseHip=r.hipY+(r.small?1.0f:2.0f);
        r.hipY=baseHip+bob;
        r.shoulderY+=bob;r.headY=bob;
        r.headAngle=lerp(1.5f,4.0f,gait);
        const float stride=r.small?lerp(6.5f,7.8f,gait):lerp(10.5f,12.5f,gait);
        r.rearAnkle=walkFoot(phase+0.5f,{27,r.hipY},baseHip+amplitude,floorY,
                             stride,r.thigh,r.shin,gait);
        r.frontAnkle=walkFoot(phase,{36,r.hipY},baseHip+amplitude,floorY,
                              stride,r.thigh,r.shin,gait);
        r.rearShoeAngle=walkingShoe(phase+0.5f,gait);
        r.frontShoeAngle=walkingShoe(phase,gait);
        // Each arm opposes the leg on its side. The forward swing pumps with a
        // folded elbow, the backward swing nearly straightens: an asymmetric
        // arc instead of both fists flapping sideways at the same height.
        // Walking carries the fist at the belly, running pumps it to the chest.
        const float lead=std::cos(cycle);
        const auto pump=[&](Point shoulderAt,float swing) {
            const float forward=(swing+1)*0.5f;
            return armReach(shoulderAt,lerp(20.0f,28.0f,gait)*swing,
                            lerp(10.0f,lerp(65.0f,95.0f,gait),forward),
                            r.upperArm,r.forearm);
        };
        r.frontHand=pump({41,r.shoulderY},-lead);
        r.rearHand=pump({23,r.shoulderY},lead);
        r.rearGesture=Gesture::Fist;r.frontGesture=Gesture::Fist;
    } else if(action==3) {
        r.skid=true;r.bodyX=-2;r.headX=-2;r.headAngle=-9;r.headY=1;
        r.rearAnkle={r.small?20.0f:18.0f,floorY};r.frontAnkle={r.small?46.0f:49.0f,floorY};
        // Braking: the leading palm pushes forwards, the trailing arm flies back.
        r.frontHand=armReach({39,shoulder},40,75,r.upperArm,r.forearm);
        r.rearHand=armReach({21,shoulder},-50,30,r.upperArm,r.forearm);
        r.rearGesture=Gesture::Open;r.frontGesture=Gesture::Open;
        r.frontShoeAngle=-12;r.rearShoeAngle=6;
    } else if(action==4) {
        // 1 while rising, 0.5 at the apex and 0 while falling.
        const float up=std::clamp(0.5f+0.5f*(rising-falling),0.0f,1.0f);
        r.headAngle=lerp(3.0f,-4.0f,up);
        r.headY=-0.5f;
        // The classic jump: the near fist punches upwards in front of the
        // chin rather than across the face; the far arm swings back. Falling
        // lowers the fist forward for the landing.
        const Point punch=r.small?Point{54.5f,52.0f}:Point{55.0f,64.0f};
        const Point land=r.small?Point{52.0f,68.0f}:Point{55.0f,87.0f};
        r.frontHand=lerp(land,punch,up);
        r.rearHand=lerp(armReach({23,shoulder},-75,50,r.upperArm,r.forearm),
                        armReach({23,shoulder},-40,18,r.upperArm,r.forearm),up);
        r.frontGesture=Gesture::Fist;r.rearGesture=Gesture::Relaxed;
        // Near knee tucks up, the far leg stretches down behind; on the way
        // down the near foot reaches for the ground.
        r.frontAnkle=lerp(Point{40,floorY-3*k},Point{42,floorY-14*k},up);
        r.rearAnkle=lerp(Point{24,floorY-10*k},Point{22,floorY-2*k},up);
        r.frontShoeAngle=lerp(-6.0f,6.0f,up);
        r.rearShoeAngle=lerp(12.0f,30.0f,up);
    } else if(action>=5 && action<=7) {
        r.swim=true;r.headAngle=-5+2.5f*std::sin(cycle);
        r.headY=0.5f*std::sin(cycle);
        r.shoulderY+=1;r.hipY-=0.6f;
        // A paddling loop below the chin: reach forwards, pull down and back,
        // fold the elbow and reach again. The far arm strokes half a cycle later.
        const auto stroke=[&](Point center,float rx,float ry,float angle) {
            const float tilt=-25*Pi/180;
            const float x=rx*std::cos(angle),y=ry*std::sin(angle);
            return Point{center.x+x*std::cos(tilt)-y*std::sin(tilt),
                         center.y+x*std::sin(tilt)+y*std::cos(tilt)};
        };
        const float s=r.small?0.74f:1.0f;
        r.frontHand=stroke({41+5*s,r.shoulderY+12*s},9*s,7*s,cycle);
        r.rearHand=stroke({23-5*s,r.shoulderY+14*s},6*s,6*s,cycle+Pi);
        r.frontGesture=Gesture::Open;r.rearGesture=Gesture::Open;
        // Bent knees and pointed toes kick alternately behind the body.
        const float kick=r.small?4.0f:6.0f;
        r.frontAnkle={33+kick*std::cos(cycle),
                      floorY-6*k+3*k*std::sin(cycle)};
        r.rearAnkle={23-kick*std::cos(cycle),
                     floorY-5*k-3*k*std::sin(cycle)};
        r.frontShoeAngle=28+10*std::sin(cycle);
        r.rearShoeAngle=28-10*std::sin(cycle);
    } else if(action>=8 && action<=9) {
        r.climb=true;r.headX=1;r.headAngle=-2;
        const float grip=(1-std::cos(cycle))*0.5f;
        // Hands take turns on the vine in front of the body, below the nose.
        // The near hand is never lifted across the face.
        r.frontHand=r.small?lerp(Point{54,52},Point{51,66},grip)
                           :lerp(Point{56,64},Point{52,82},grip);
        r.rearHand=r.small?lerp(Point{40,68},Point{45,58},grip)
                          :lerp(Point{42,84},Point{46,69},grip);
        r.frontGesture=Gesture::Grip;r.rearGesture=Gesture::Grip;
        // The opposite foot steps up with each reach.
        r.frontAnkle={41,floorY-11*k*grip};
        r.rearAnkle={25,floorY-11*k*(1-grip)};
        r.frontShoeAngle=-6*grip;r.rearShoeAngle=-6*(1-grip);
        r.headY=0.4f*std::sin(cycle);
    } else if(action==10) {
        r.crouch=true;r.headY=32;r.headAngle=2;
        r.shoulderY=99;r.hipY=110;
        r.rearAnkle={18,121};r.frontAnkle={45,121};
        r.rearHand={18,113};r.frontHand={47,112};
        r.frontArmBend=1;
        r.frontGesture=Gesture::Relaxed;r.rearGesture=Gesture::Relaxed;
    } else if(action==11) {
        r.throwBall=true;r.headX=-0.5f;r.headAngle=-2;
        // The throwing arm sweeps from the hip to a forward release and back.
        const float extension=animated?std::sin(phase*Pi):1.0f;
        r.frontHand=r.small?lerp(Point{47,72},Point{54,63},extension)
                           :lerp(Point{47,93},Point{55,79},extension);
        r.rearHand=armReach({23,shoulder},-30,20,r.upperArm,r.forearm);
        r.frontGesture=Gesture::Throw;
        r.rearAnkle={23,floorY};r.frontAnkle={40,floorY};
    } else if(action==22) {
        // A frontal surprise pose: both elbows point outwards and the open
        // hands rise beside the face rather than over it.
        r.death=true;r.headAngle=-4-5*rising+8*falling;
        r.rearArmBend=1;r.frontArmBend=-1;
        r.rearHand={12,shoulder-2-3*rising};
        r.frontHand={r.small?53.0f:55.0f,shoulder-2-3*rising};
        r.rearGesture=Gesture::Open;r.frontGesture=Gesture::Open;
        r.rearAnkle={20,floorY-6*k-3*k*rising};
        r.frontAnkle={45,floorY-5*k-3*k*rising};
        r.rearShoeAngle=-18+6*falling;r.frontShoeAngle=18-6*falling;
    } else if(animated) {
        const float breath=0.3f*std::sin(cycle);
        r.headY=breath;r.shoulderY+=breath*0.5f;
    }
    if(r.small) {
        // The head is scaled uniformly around its own center, not compressed
        // vertically. The compact body has its own shoulder, hip and bones.
        r.headY-=5.4f;
    }
    (void)hip;
    return r;
}

void solveRig(Rig& r) {
    Point rearHip{27+r.bodyX,r.hipY},frontHip{36+r.bodyX,r.hipY};
    Point rearShoulder{23+r.bodyX,r.shoulderY};
    Point frontShoulder{41+r.bodyX,r.shoulderY};
    solveLimb(rearHip,r.rearAnkle,r.thigh,r.shin,r.crouch?-1:1,r.rearKnee);
    solveLimb(frontHip,r.frontAnkle,r.thigh,r.shin,1,r.frontKnee);
    solveLimb(rearShoulder,r.rearHand,r.upperArm,r.forearm,r.rearArmBend,r.rearElbow);
    solveLimb(frontShoulder,r.frontHand,r.upperArm,r.forearm,r.frontArmBend,r.frontElbow);
}

Rig completeRig(const ChibiMotion& motion) {
    Rig r=rigForMotion(motion);
    if(motion.lowerPose>=0 && motion.lowerPose<=25) {
        ChibiMotion lower;
        lower.nativePose=motion.lowerPose;lower.phase=motion.lowerPhase;
        lower.verticalSpeed=motion.verticalSpeed;lower.gait=motion.gait;
        Rig legs=rigForMotion(lower);
        const float shift=legs.hipY-r.hipY;
        r.hipY=legs.hipY;r.shoulderY+=shift;r.headY+=shift;
        r.rearHand.y+=shift;r.frontHand.y+=shift;
        r.rearAnkle=legs.rearAnkle;r.frontAnkle=legs.frontAnkle;
        r.rearShoeAngle=legs.rearShoeAngle;r.frontShoeAngle=legs.frontShoeAngle;
    }
    solveRig(r);
    return r;
}

float handAngle(Point elbow,Point hand) {
    const float dx=hand.x-elbow.x,dy=hand.y-elbow.y;
    return std::atan2(dy,dx)*180/Pi-90;
}
Point thumbPad(Gesture gesture,bool rear) {
    const float side=rear?1.0f:-1.0f;
    if(gesture==Gesture::Open || gesture==Gesture::Throw)
        return {side*5.0f,1.0f};
    if(gesture==Gesture::Grip) return {side*4.1f,0.5f};
    return {side*4.0f,-0.2f};
}

void glove(Painter& p,Point center,float angle,Color edge,bool rear,
           Gesture gesture) {
    p.local(0,0,angle,center);
    const Color white{250,246,233},shadow{187,203,221},crease{138,157,180};
    const float side=rear?1.0f:-1.0f;
    const bool open=gesture==Gesture::Open || gesture==Gesture::Throw;
    const bool grip=gesture==Gesture::Grip;
    const bool fist=gesture==Gesture::Fist || grip;
    if(open) {
        p.outlineEllipse(center.x,center.y,4.2f,4.1f,white,edge,0.7f);
        // Four separate fingers, with rounded pads and three fine joints.
        const std::array<float,4> length{6.0f,7.8f,7.2f,5.8f};
        for(int i=0;i<4;++i) {
            const float xx=center.x-3.0f+i*2.0f;
            const float spread=(i-1.5f)*0.35f;
            const Point a{xx,center.y+1.0f};
            const int finger=rear?3-i:i;
            const Point b{xx+spread,center.y+length[finger]};
            p.capsule(a,b,2.0f,white,edge,0.45f);
            p.line({b.x-0.5f,b.y-2.4f},{b.x+0.5f,b.y-2.4f},
                   0.38f,shadow);
        }
        p.capsule({center.x+side*3.1f,center.y-1.2f},
                  {center.x+side*6.0f,center.y+2.4f},2.7f,white,edge,0.55f);
        p.curve({center.x-side*1.8f,center.y-1.5f},
                {center.x+side*1.5f,center.y-2},
                {center.x+side*2.2f,center.y+1},
                {center.x+side*1.4f,center.y+2.2f},0.5f,crease);
        p.curve({center.x-side*1.7f,center.y+0.3f},
                {center.x-side*1.2f,center.y+2},
                {center.x+side*0.1f,center.y+2.5f},
                {center.x+side*1.8f,center.y+2.7f},0.42f,shadow);
    } else {
        const float fingerEnd=fist?4.5f:5.5f;
        p.outlineEllipse(center.x,center.y+0.4f,4.8f,fingerEnd,white,edge,0.75f);
        p.ellipse(center.x-side*1.8f,center.y+1.7f,2.7f,2.8f,shadow,0.26f);
        // Knuckle bumps and seams follow the fingers, instead of two lines
        // stamped across an undifferentiated white oval.
        for(int i=0;i<3;++i) {
            const float xx=center.x+side*(-2.4f+i*2.0f);
            p.curve({xx-0.6f,center.y+fingerEnd-1.4f},
                    {xx-0.8f,center.y+fingerEnd-2.6f},
                    {xx+0.6f,center.y+fingerEnd-2.7f},
                    {xx+0.8f,center.y+fingerEnd-1.5f},0.45f,crease);
        }
        const Point thumb=thumbPad(gesture,rear);
        p.outlineEllipse(center.x+thumb.x,center.y+thumb.y,
                         1.95f,2.7f,white,edge,0.55f);
        p.curve({center.x+side*3.5f,center.y-1.2f},
                {center.x+side*2.7f,center.y-0.3f},
                {center.x+side*2.8f,center.y+1.7f},
                {center.x+side*3.2f,center.y+2.1f},0.48f,shadow);
        p.ellipse(center.x+side*4.0f,center.y-0.8f,0.75f,0.9f,
                  {255,255,246},0.85f);
    }
    // The cuff is perpendicular to the forearm and distinct from the palm.
    p.capsule({center.x-3.5f,center.y-5.1f},
              {center.x+3.5f,center.y-5.1f},2.7f,{238,234,222},edge,0.6f);
    p.line({center.x-2.8f,center.y-5.7f},{center.x+2.8f,center.y-5.7f},
           0.45f,{255,253,237});
    p.line({center.x-2.5f,center.y-4.4f},{center.x+2.5f,center.y-4.4f},
           0.45f,shadow);
    p.resetLocal();
}

void arm(Painter& p,Point shoulder,Point elbow,Point hand,Color shirt,
         Color edge,bool rear,Gesture gesture) {
    const Color fabric=rear?dark(shirt,0.12f):shirt;
    p.capsule(shoulder,elbow,9.0f,fabric,edge,0.9f);
    const float dx=hand.x-elbow.x,dy=hand.y-elbow.y;
    const float length=std::max(0.001f,std::hypot(dx,dy));
    const Point wrist{hand.x-dx/length*5.0f,hand.y-dy/length*5.0f};
    p.capsule(elbow,wrist,7.7f,fabric,edge,0.9f);
    const Point n{-dy/length,dx/length};
    p.line({elbow.x+n.x*1.5f,elbow.y+n.y*1.5f},
           {wrist.x+n.x*1.5f,wrist.y+n.y*1.5f},0.85f,light(fabric,0.28f));
    p.line({elbow.x-n.x*2,elbow.y-n.y*2},
           {elbow.x+n.x*1.5f,elbow.y+n.y*1.5f},0.65f,dark(fabric,0.28f));
    glove(p,hand,handAngle(elbow,hand),edge,rear,gesture);
}

void shoe(Painter& p, Point ankle, float angle, bool rear, Color edge) {
    p.local(0,0,angle,{ankle.x,ankle.y+1});
    const Color leather=rear?Color{105,57,45}:Color{133,71,45};
    const float x=ankle.x,y=ankle.y;
    // A rounded toe and a low heel, with a separate rubber outsole.
    std::vector<Point> shoePoints{{x-5.6f,y-2.2f},{x+2.7f,y-3.3f},
        {x+7.4f,y-1.8f},{x+9.1f,y+0.7f},{x+9.5f,y+4.0f},
        {x+7.2f,y+5.4f},{x-5.6f,y+5.4f},{x-6.5f,y+3.1f}};
    p.outline(shoePoints,leather,edge,1.0f);
    p.polygon({{x-6,y+2.7f},{x+9,y+2.7f},{x+9,y+4.2f},
               {x+7.2f,y+5.3f},{x-5.2f,y+5.3f}},Color{58,39,42});
    p.curve({x-3.8f,y-1.4f},{x+0.2f,y-2.8f},{x+5,y-2.3f},
            {x+7,y-0.6f},1.2f,light(leather,0.48f));
    p.line({x-1,y-1.7f},{x-1,y+1.7f},0.55f,dark(leather,0.40f));
    p.line({x-4.5f,y+3.4f},{x+6.7f,y+3.4f},0.45f,{183,124,86});
    p.resetLocal();
}

void leg(Painter& p, Point hip, Point knee, Point ankle,
         Color denim, Color edge, bool rear) {
    Color fill=rear?dark(denim,0.14f):denim;
    p.capsule(hip,knee,11.5f,fill,edge,1.0f);
    p.capsule(knee,ankle,10.7f,fill,edge,1.0f);
    p.line({hip.x-2,hip.y+2},{knee.x-2,knee.y},1.4f,light(fill,0.23f));
    p.line({knee.x+3.0f,knee.y},{ankle.x+3,ankle.y-1},1.4f,dark(fill,0.20f));
    p.line({ankle.x-4,ankle.y-3},{ankle.x+3,ankle.y-3},1.0f,light(fill,0.28f));
}

void torso(Painter& p,const Rig& r, Color shirt,Color denim,Color edge) {
    const float top=r.shoulderY-8, bottom=r.hipY+4;
    p.outline({{24,top},{39,top},{44,r.shoulderY+6},
        {43,bottom-4},{38,bottom},{26,bottom},{21,bottom-5},{20,r.shoulderY+4}},
        shirt,edge,1.1f);
    p.polygon({{21,top+6},{27,top+3},{28,bottom-1},{23,bottom-3}},
              dark(shirt,0.23f));
    p.polygon({{27,top+2},{34,top+1},{39,top+6},{30,top+7}},
              light(shirt,0.25f));
    const float bibTop=r.shoulderY+2;
    p.outline({{25,bibTop},{38,bibTop},{41,bibTop+8},
        {40,bottom-2},{36,bottom+1},{26,bottom},{23,bottom-5}},
        denim,edge,0.7f);
    p.polygon({{25,bibTop+2},{29,bibTop+2},{30,bottom-1},
              {26,bottom-1},{24,bottom-6}},light(denim,0.25f));
    p.polygon({{37,bibTop+2},{39,bibTop+6},{39,bottom-3},
              {36,bottom-2},{35,bibTop+5}},dark(denim,0.20f));
    p.capsule({25,top+3},{26,bibTop+6},3.9f,denim,edge,0.55f);
    p.capsule({38,top+3},{37,bibTop+6},3.9f,denim,edge,0.55f);
    p.line({24.8f,top+4},{25.8f,bibTop+3},0.6f,light(denim,0.48f));
    p.line({37.8f,top+4},{36.8f,bibTop+3},0.6f,light(denim,0.38f));
    for(float x : {26.0f,37.0f}) {
        p.outlineEllipse(x,bibTop+5,1.55f,1.55f,{247,183,48},{111,65,38},0.5f);
        p.ellipse(x-0.4f,bibTop+4.5f,0.55f,0.55f,{255,248,148});
    }
    const float pocketY=std::min(bibTop+14,bottom-3);
    if(pocketY>bibTop+7) {
        p.curve({28,pocketY-4},{30,pocketY-3},{34,pocketY-3},{36,pocketY-4},
                0.7f,light(denim,0.36f));
        p.curve({28,pocketY-4},{28,pocketY+2},{34,pocketY+3},{36,pocketY-4},
                0.65f,light(denim,0.28f));
        p.ellipse(29.2f,pocketY-2.2f,0.4f,0.4f,{222,175,114});
    }
    p.curve({29,bottom-2},{30,bottom-6},{33,bottom-6},{34,bottom-2},
            0.65f,dark(denim,0.32f));
}

std::vector<Point> cubicPath(Point start,
                             std::initializer_list<std::array<Point,3>> segments) {
    std::vector<Point> result{start};
    Point a=start;
    for(const auto& segment : segments) {
        const Point b=segment[0],c=segment[1],d=segment[2];
        for(int i=1;i<=12;++i) {
            const float t=i/12.0f,u=1-t;
            result.push_back({u*u*u*a.x+3*u*u*t*b.x+3*u*t*t*c.x+t*t*t*d.x,
                              u*u*u*a.y+3*u*u*t*b.y+3*u*t*t*c.y+t*t*t*d.y});
        }
        a=d;
    }
    return result;
}

void head(Painter& p,const Rig& r,const ChibiColors& colors,Color edge) {
    p.local(r.headX,r.headY,r.headAngle,{32,34},r.small?0.82f:1.0f);
    const Color cap=color(colors.cap);
    const Color skin{249,197,158},skinShadow{209,136,118};
    const Color hair{82,46,39},hairShadow{46,30,36};
    // Layered side hair: rounded locks replace the original staircase profile.
    auto hairPath=cubicPath({14,24}, {
        {{{6,30},{7,44},{11,48}}},
        {{{6,53},{12,61},{18,61}}},
        {{{14,67},{23,66},{28,60}}},
        {{{21,46},{20,35},{24,26}}},
        {{{20,23},{16,23},{14,24}}}
    });
    p.outline(hairPath,hair,edge,1.1f);
    p.polygon({{9,45},{15,48},{13,57},{18,58},{17,63},{11,58}},
              hairShadow);
    p.curve({12,29},{8,38},{11,42},{14,46},1.2f,light(hair,0.25f));
    p.curve({12,52},{13,56},{17,58},{20,58},0.85f,light(hair,0.26f));
    // A slight jaw and chin keep the face expressive, rather than a circle.
    auto face=cubicPath({16,26}, {
        {{{19,21},{36,22},{43,29}}},
        {{{47,35},{50,40},{49,47}}},
        {{{54,50},{55,56},{49,60}}},
        {{{42,67},{25,69},{18,61}}},
        {{{11,53},{11,38},{16,26}}}
    });
    p.outline(face,skin,edge,1.1f);
    auto faceShade=cubicPath({16,31}, {
        {{{11,40},{13,55},{19,60}}},
        {{{22,64},{28,66},{34,65}}},
        {{{24,60},{22,53},{23,46}}},
        {{{21,42},{20,36},{16,31}}}
    });
    p.polygon(faceShade,skinShadow,0.48f);
    p.ellipse(33,45,12.0f,15.0f,{255,224,188},0.22f);
    p.outlineEllipse(16,47,5.5f,7.1f,skin,edge,0.9f);
    p.curve({14,45},{18,40},{20,48},{16,50},0.8f,skinShadow);
    p.ellipse(15.5f,43.7f,1.9f,2.0f,{255,222,183},0.5f);
    p.polygon({{20,30},{25,29},{25,34},{23,36},{24,43},{21,46},{20,39}},hair);
    p.curve({21,31},{22,34},{21,37},{23,40},0.65f,light(hair,0.30f));

    if(r.death) {
        // Native death keeps its upward leap; only the facial pose changes.
        p.curve({31,39},{33,36},{36,36},{38,39},1.5f,hairShadow);
        p.curve({42,39},{45,36},{48,37},{49,40},1.35f,hairShadow);
        p.line({31,44},{37,48},1.4f,hairShadow);
        p.line({31,48},{37,44},1.4f,hairShadow);
        p.line({43,44},{48,48},1.25f,hairShadow);
        p.line({43,48},{48,44},1.25f,hairShadow);
    } else {
        // Blue irises, large white reflections and a tiny lower catchlight.
        p.outlineEllipse(34.6f,43.4f,5.1f,8.0f,{255,254,242},hairShadow,0.65f);
        p.outlineEllipse(45.7f,42.9f,3.6f,6.6f,{255,254,242},hairShadow,0.55f);
        p.ellipse(36.0f,43.8f,3.55f,6.6f,{41,104,171});
        p.ellipse(46.6f,43.3f,2.7f,5.5f,{41,104,171});
        p.ellipse(36.5f,44.2f,1.85f,4.7f,{29,43,77});
        p.ellipse(47.0f,43.8f,1.4f,3.9f,{29,43,77});
        p.ellipse(35.0f,40.3f,1.6f,2.0f,{255,255,255});
        p.ellipse(45.6f,40.2f,1.25f,1.55f,{255,255,255});
        p.ellipse(37.0f,47.5f,0.7f,0.75f,{126,224,237});
        p.ellipse(47.4f,46.2f,0.5f,0.6f,{126,224,237});
        p.curve({29.7f,34.8f},{33,32.5f},{36.5f,33.0f},{39,35.1f},
                r.skid?1.9f:1.5f,hairShadow);
        p.curve({42,34.7f},{44.5f,33.3f},{47,34.0f},{49,35.9f},
                r.skid?1.65f:1.3f,hairShadow);
        p.curve({31,51},{33,52.8f},{35,53},{37,52.2f},0.55f,{215,137,117});
    }
    p.ellipse(25.5f,54,4.0f,2.1f,{233,117,122},0.22f);
    // A rounded anime nose, with the light plane on its top-left side.
    p.outlineEllipse(50.5f,50.6f,7.25f,5.95f,skin,edge,0.85f);
    p.ellipse(53.2f,52.8f,4.1f,2.4f,skinShadow,0.26f);
    p.ellipse(48.2f,48.9f,3.2f,1.4f,{255,225,189},0.75f);
    p.ellipse(55,52.1f,0.65f,0.45f,{204,133,114},0.7f);
    // The familiar rounded moustache follows the mouth, with separated lobes.
    auto moustache=cubicPath({29,53.3f}, {
        {{{32,52},{36,54.3f},{40,55.0f}}},
        {{{45,53.3f},{49,54},{51,57.5f}}},
        {{{49,60.4f},{45,61.7f},{42,60.5f}}},
        {{{39,63},{35,61.6f},{34,60.2f}}},
        {{{30,60.3f},{28,57.8f},{29,53.3f}}}
    });
    p.outline(moustache,hairShadow,edge,0.4f);
    p.curve({31,55.3f},{34,55.0f},{37,57.8f},{39,57.3f},
            0.8f,light(hair,0.12f));
    p.curve({42,56.8f},{45,55.2f},{47,56.2f},{49,57.6f},
            0.65f,light(hair,0.10f));
    if(r.death) {
        p.outlineEllipse(41,63,2.2f,2.3f,{109,43,49},edge,0.45f);
        p.ellipse(41,63.9f,1.4f,0.7f,{239,141,148});
    } else {
        p.curve({35,62.7f},{38,64.3f},{42,64.0f},{44,62.3f},0.7f,{151,77,74});
        p.curve({36.5f,62.6f},{38.5f,63.3f},{40.2f,63.5f},{42,62.8f},
                0.55f,{255,233,202});
    }

    // Dome and curved brim, with an embroidered badge and a warm light plane.
    auto hat=cubicPath({9.4f,25.2f}, {
        {{{8,17},{12,8},{24,5.2f}}},
        {{{37,1.7f},{48,6.5f},{49.0f,19.4f}}},
        {{{49.2f,23},{48,27},{45,28.3f}}},
        {{{33,27.3f},{20,28.3f},{9.4f,25.2f}}}
    });
    p.outline(hat,cap,edge,1.2f);
    auto hatShade=cubicPath({14,9.3f}, {
        {{{10,16},{10,21},{12,24.9f}}},
        {{{20,26.6f},{27,26.8f},{29,25.5f}}},
        {{{24,22},{20,16},{20,7.0f}}},
        {{{18,7.5f},{16,8.1f},{14,9.3f}}}
    });
    p.polygon(hatShade,dark(cap,0.25f));
    auto hatLight=cubicPath({23,8}, {
        {{{33,4.1f},{42,8.3f},{44.9f,16.3f}}},
        {{{45.4f,19.2f},{41.8f,20.0f},{40.5f,16.4f}}},
        {{{36.5f,10.4f},{31.8f,9.0f},{23,10.8f}}},
        {{{22,10.6f},{21.7f,8.8f},{23,8}}}
    });
    p.polygon(hatLight,light(cap,0.35f),0.70f);
    p.curve({18,8.8f},{14,14},{14,20},{16,24.8f},
            0.6f,light(cap,0.25f));
    p.curve({29,5.8f},{30,10},{29,15},{27,24.2f},
            0.65f,dark(cap,0.16f));
    p.ellipse(34.6f,16.8f,7.15f,7.4f,dark(cap,0.20f));
    p.outlineEllipse(35.0f,16.2f,6.6f,6.8f,{255,244,222},edge,0.35f);
    p.ellipse(33.7f,14.3f,4.4f,3.5f,{255,255,246},0.7f);
    const Color mark=dark(cap,0.04f);
    if(colors.luigi) {
        p.line({33.0f,12.4f},{33.0f,19.9f},1.8f,mark);
        p.line({33.0f,19.9f},{38.2f,19.9f},1.8f,mark);
    } else {
        p.line({30.9f,19.6f},{31.7f,12.2f},1.65f,mark);
        p.line({31.7f,12.2f},{35.0f,17.0f},1.6f,mark);
        p.line({35.0f,17.0f},{38.2f,12.2f},1.6f,mark);
        p.line({38.2f,12.2f},{39.0f,19.6f},1.65f,mark);
    }
    auto brim=cubicPath({20,25.0f}, {
        {{{30,23.1f},{43,23.2f},{51,25.0f}}},
        {{{58,26.1f},{62,28.1f},{60,30.2f}}},
        {{{51,32.6f},{35,30.1f},{21,30.1f}}},
        {{{18,29.4f},{17.9f,26.0f},{20,25.0f}}}
    });
    p.outline(brim,cap,edge,0.9f);
    p.curve({21,28.5f},{34,28.5f},{50,31.2f},{59,29.3f},
            1.2f,dark(cap,0.37f));
    p.curve({24,25.3f},{39,24.4f},{50,25.6f},{57,27.7f},
            0.7f,light(cap,0.48f));
    p.resetLocal();
}

} // namespace


void renderChibiMotion(const ChibiMotion& motion,const ChibiColors& colors,
                        std::vector<unsigned char>& rgba) {
    const int pose=std::clamp(motion.nativePose,0,25);
    const bool small=pose>=12 && pose<=23;
    Painter p(small,pose==24);
    const Rig r=completeRig(motion);
    const Color edge{43,30,47};
    const Color shirt=color(colors.shirt),denim=color(colors.overalls);
    const Point rearHip{27+r.bodyX,r.hipY},frontHip{36+r.bodyX,r.hipY};
    const Point rearShoulder{23+r.bodyX,r.shoulderY};
    const Point frontShoulder{41+r.bodyX,r.shoulderY};

    leg(p,rearHip,r.rearKnee,r.rearAnkle,denim,edge,true);
    shoe(p,r.rearAnkle,r.rearShoeAngle,true,edge);
    arm(p,rearShoulder,r.rearElbow,r.rearHand,shirt,edge,true,r.rearGesture);
    leg(p,frontHip,r.frontKnee,r.frontAnkle,denim,edge,false);
    shoe(p,r.frontAnkle,r.frontShoeAngle,false,edge);
    p.local(r.bodyX,0);
    torso(p,r,shirt,denim,edge);
    p.outlineEllipse(32,r.shoulderY-6,7,5.0f,{239,177,142},edge,0.65f);
    p.polygon({{25,r.shoulderY-8},{28,r.shoulderY-3},{33,r.shoulderY-2},
               {39,r.shoulderY-7}},dark(shirt,0.12f));
    p.resetLocal();
    head(p,r,colors,edge);
    arm(p,frontShoulder,r.frontElbow,r.frontHand,shirt,edge,false,r.frontGesture);
    p.finish(rgba);
}

void renderChibiPose(int pose,const ChibiColors& colors,
                     std::vector<unsigned char>& rgba) {
    ChibiMotion motion;
    motion.nativePose=pose;
    renderChibiMotion(motion,colors,rgba);
}
} // namespace mario
