/* This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Dai Yang
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * Raytracing Example 
 * 
 * Original Code by 
 * Copyright (C) 2012  www.scratchapixel.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * MPI/LAIK Modification by Dai Yang
 */

#include <cstdlib> 
#include <cstdio> 
#include <cmath> 
#include <fstream> 
#include <vector> 
#include <iostream> 
#include <cassert>
#include <cstring>

extern "C" {
#include <laik.h>
}

typedef struct _programops{
    int width;
    int height;
    int max_depth;
}programops_t;

static int  MAX_RAY_DEPTH = 10;

static inline  
double mix(const double &a, const double &b, const double &mix) 
{ 
    return b * mix + a * (1 - mix); 
}

programops_t handleCmdlineArgs(
    int argc, 
    char** argv
){
    int arg = 1;
    programops_t ops;
    memset(&ops, 0x0, sizeof(ops));
    while((arg < argc) && (argv[arg][0] == '-')) {
        if (argv[arg][1] == 'w') {
            arg++;
            if(arg < argc){
                ops.width = atoi(argv[arg]);
            }
        } 
        if (argv[arg][1] == 'h') {
            arg++;
            if(arg < argc){
                ops.height = atoi(argv[arg]);
            }
        } 
        if (argv[arg][1] == 'd'){
            arg++;
            if(arg < argc){
                ops.max_depth = atoi(argv[arg]);
            }
        } 
        if (argv[arg][1] == '?') {
            printf("raytracer [options]\n"
                   "\nOptions:\n"
                   " -w: width of output image\n"
                   " -h: height of the output image\n"
                   " -d: max recursive depth\n");
            exit(1);
        }
        arg++;
    }

    return ops;
}

template<typename T> 
class Vec3 
{ 
public: 
    T x, y, z; 
    Vec3() : x(T(0)), y(T(0)), z(T(0)) {} 
    Vec3(T xx) : x(xx), y(xx), z(xx) {} 
    Vec3(T xx, T yy, T zz) : x(xx), y(yy), z(zz) {} 
    Vec3& normalize() 
    { 
        T nor2 = length2(); 
        if (nor2 > 0) { 
            T invNor = 1 / sqrt(nor2); 
            x *= invNor, y *= invNor, z *= invNor; 
        } 
        return *this; 
    } 
    Vec3<T> operator * (const T &f) const { return Vec3<T>(x * f, y * f, z * f); } 
    Vec3<T> operator * (const Vec3<T> &v) const { return Vec3<T>(x * v.x, y * v.y, z * v.z); } 
    T dot(const Vec3<T> &v) const { return x * v.x + y * v.y + z * v.z; } 
    Vec3<T> operator - (const Vec3<T> &v) const { return Vec3<T>(x - v.x, y - v.y, z - v.z); } 
    Vec3<T> operator + (const Vec3<T> &v) const { return Vec3<T>(x + v.x, y + v.y, z + v.z); } 
    Vec3<T>& operator += (const Vec3<T> &v) { x += v.x, y += v.y, z += v.z; return *this; } 
    Vec3<T>& operator *= (const Vec3<T> &v) { x *= v.x, y *= v.y, z *= v.z; return *this; } 
    Vec3<T> operator - () const { return Vec3<T>(-x, -y, -z); } 
    T length2() const { return x * x + y * y + z * z; } 
    T length() const { return sqrt(length2()); } 
    friend std::ostream & operator << (std::ostream &os, const Vec3<T> &v) 
    { 
        os << "[" << v.x << " " << v.y << " " << v.z << "]"; 
        return os; 
    } 
}; 
 
typedef Vec3<double> Vec3f; 

class Sphere 
{ 
public: 
    Vec3f center;                           /// position of the sphere 
    double radius, radius2;                  /// sphere radius and radius^2 
    Vec3f surfaceColor, emissionColor;      /// surface color and emission (light) 
    double transparency, reflection;         /// surface transparency and reflectivity 
    Sphere( 
        const Vec3f &c, 
        const double &r, 
        const Vec3f &sc, 
        const double &refl = 0, 
        const double &transp = 0, 
        const Vec3f &ec = 0) : 
        center(c), radius(r), radius2(r * r), surfaceColor(sc), emissionColor(ec), 
        transparency(transp), reflection(refl) 
    { /* empty */ } 
    bool intersect(const Vec3f &rayorig, const Vec3f &raydir, double &t0, double &t1) const 
    { 
        Vec3f l = center - rayorig; 
        double tca = l.dot(raydir); 
        if (tca < 0) return false; 
        double d2 = l.dot(l) - tca * tca; 
        if (d2 > radius2) return false; 
        double thc = sqrt(radius2 - d2); 
        t0 = tca - thc; 
        t1 = tca + thc; 
 
        return true; 
    } 
}; 

Vec3f trace( 
    const Vec3f &rayorig, 
    const Vec3f &raydir, 
    const std::vector<Sphere> &spheres, 
    const int &depth) 
{ 
    //if (raydir.length() != 1) std::cerr << "Error " << raydir << std::endl;
    double tnear = INFINITY; 
    const Sphere* sphere = NULL; 
    // find intersection of this ray with the sphere in the scene
    for (unsigned i = 0; i < spheres.size(); ++i) { 
        double t0 = INFINITY, t1 = INFINITY; 
        if (spheres[i].intersect(rayorig, raydir, t0, t1)) { 
            if (t0 < 0) t0 = t1; 
            if (t0 < tnear) { 
                tnear = t0; 
                sphere = &spheres[i]; 
            } 
        } 
    } 
    // if there's no intersection return black or background color
    if (!sphere) return Vec3f(2); 
    Vec3f surfaceColor = 0; // color of the ray/surfaceof the object intersected by the ray 
    Vec3f phit = rayorig + raydir * tnear; // point of intersection 
    Vec3f nhit = phit - sphere->center; // normal at the intersection point 
    nhit.normalize(); // normalize normal direction 
    // If the normal and the view direction are not opposite to each other
    // reverse the normal direction. That also means we are inside the sphere so set
    // the inside bool to true. Finally reverse the sign of IdotN which we want
    // positive.
    double bias = 1e-4; // add some bias to the point from which we will be tracing 
    bool inside = false; 
    if (raydir.dot(nhit) > 0) nhit = -nhit, inside = true; 
    if ((sphere->transparency > 0 || sphere->reflection > 0) && depth < MAX_RAY_DEPTH) { 
        double facingratio = -raydir.dot(nhit); 
        // change the mix value to tweak the effect
        double fresneleffect = mix(pow(1 - facingratio, 3), 1, 0.1); 
        // compute reflection direction (not need to normalize because all vectors
        // are already normalized)
        Vec3f refldir = raydir - nhit * 2 * raydir.dot(nhit); 
        refldir.normalize(); 
        Vec3f reflection = trace(phit + nhit * bias, refldir, spheres, depth + 1); 
        Vec3f refraction = 0; 
        // if the sphere is also transparent compute refraction ray (transmission)
        if (sphere->transparency) { 
            double ior = 1.1, eta = (inside) ? ior : 1 / ior; // are we inside or outside the surface? 
            double cosi = -nhit.dot(raydir); 
            double k = 1 - eta * eta * (1 - cosi * cosi); 
            Vec3f refrdir = raydir * eta + nhit * (eta *  cosi - sqrt(k)); 
            refrdir.normalize(); 
            refraction = trace(phit - nhit * bias, refrdir, spheres, depth + 1); 
        } 
        // the result is a mix of reflection and refraction (if the sphere is transparent)
        surfaceColor = ( 
            reflection * fresneleffect + 
            refraction * (1 - fresneleffect) * sphere->transparency) * sphere->surfaceColor; 
    } 
    else { 
        // it's a diffuse object, no need to raytrace any further
        for (unsigned i = 0; i < spheres.size(); ++i) { 
            if (spheres[i].emissionColor.x > 0) { 
                // this is a light
                Vec3f transmission = 1; 
                Vec3f lightDirection = spheres[i].center - phit; 
                lightDirection.normalize(); 
                for (unsigned j = 0; j < spheres.size(); ++j) { 
                    if (i != j) { 
                        double t0, t1; 
                        if (spheres[j].intersect(phit + nhit * bias, lightDirection, t0, t1)) { 
                            transmission = 0; 
                            break; 
                        } 
                    } 
                } 
                surfaceColor += sphere->surfaceColor * transmission * 
                std::max(double(0), nhit.dot(lightDirection)) * spheres[i].emissionColor; 
            } 
        } 
    } 
 
    return surfaceColor + sphere->emissionColor; 
} 

int main(int argc, char **argv) 
{ 
    Laik_Instance* inst = laik_init (&argc, &argv);
    Laik_Group* world = laik_world(inst);
    laik_enable_profiling(inst);
    
    programops_t ops = handleCmdlineArgs(argc, argv);

    //std::cout << ops.width << ";" << ops.height << ";" << ops.max_depth <<std::endl;
    unsigned width = ops.width > 0 ?  ops.width : 640;
    unsigned height = ops.height > 0 ? ops.height : 480;
    MAX_RAY_DEPTH = ops.max_depth > 0 ? ops.max_depth : MAX_RAY_DEPTH;

    std::vector<Sphere> spheres; 
    // position, radius, surface color, reflectivity, transparency, emission color
    spheres.push_back(Sphere(Vec3f( 0.0, -10004, -20), 10000, Vec3f(0.20, 0.20, 0.20), 0, 0.0)); 
    spheres.push_back(Sphere(Vec3f( 0.0, -1000, -20), 10000, Vec3f(0.20, 0.20, 0.20), 0, 0.0));
    spheres.push_back(Sphere(Vec3f( 0.0, -10, -20), 10000, Vec3f(0.20, 0.20, 0.20), 0, 0.0));
    spheres.push_back(Sphere(Vec3f( 0.0, -104, -20), 10000, Vec3f(0.20, 0.20, 0.20), 0, 0.0));
    spheres.push_back(Sphere(Vec3f( 0.0, -10504, -20), 10000, Vec3f(0.20, 0.20, 0.20), 0, 0.0));
    spheres.push_back(Sphere(Vec3f( 0.0,      0, -20),     4, Vec3f(1.00, 0.32, 0.36), 1, 0.5)); 
    spheres.push_back(Sphere(Vec3f( 5.0,     -1, -15),     2, Vec3f(0.90, 0.76, 0.46), 1, 0.0)); 
    spheres.push_back(Sphere(Vec3f( 5.0,      0, -25),     3, Vec3f(0.65, 0.77, 0.97), 1, 0.0)); 
    spheres.push_back(Sphere(Vec3f(-5.5,      0, -15),     3, Vec3f(0.90, 0.90, 0.90), 1, 0.0)); 
    // light
    spheres.push_back(Sphere(Vec3f( 0.0,     20, -30),     3, Vec3f(0.00, 0.00, 0.00), 0, 0.0, Vec3f(3))); 
        
    size_t sz_image = width * height;

    Laik_Space* space = laik_new_space_1d(inst, sz_image);
    Laik_Space* space2d = laik_new_space_2d(inst, width, height);
    Laik_Data* xval = laik_new_data(space, laik_Double);
    Laik_Data* yval = laik_new_data(space, laik_Double);
    Laik_Data* zval = laik_new_data(space, laik_Double);

    Laik_Partitioner* parter = laik_new_bisection_partitioner();
    Laik_Partitioning* pImage = laik_new_partitioning(parter, world, space2d, 0);

    double *xvalues;
    double *yvalues;
    double *zvalues;

    double invWidth = 1 / double(width), invHeight = 1 / double(height); 
    double fov = 30, aspectratio = width / double(height); 
    double angle = tan(M_PI * 0.5 * fov / 180.); 

    Laik_Partitioning* pValAll = laik_new_partitioning(laik_All, world, space, 0);
    laik_switchto_partitioning(xval, pValAll, LAIK_DF_Init, LAIK_RO_Sum);
    laik_switchto_partitioning(yval, pValAll, LAIK_DF_Init, LAIK_RO_Sum);
    laik_switchto_partitioning(zval, pValAll, LAIK_DF_Init, LAIK_RO_Sum);

    int64_t xstart, xend, ystart, yend;
    laik_my_slice_2d(pImage, 0, &xstart, &xend, &ystart, &yend);

    laik_map_def1(xval, (void**)&xvalues, 0);
    laik_map_def1(yval, (void**)&yvalues, 0);
    laik_map_def1(zval, (void**)&zvalues, 0);

    // Trace rays
    for (unsigned y = ystart; y < yend; ++y) { 
        for (unsigned x = xstart; x < xend; ++x) { 
            Vec3f pixel;
            double xx = (2 * ((x + 0.5) * invWidth) - 1) * angle * aspectratio; 
            double yy = (1 - 2 * ((y + 0.5) * invHeight)) * angle; 
            Vec3f raydir(xx, yy, -1); 
            raydir.normalize(); 
            pixel = trace(Vec3f(0), raydir, spheres, 0);
            xvalues[width*y+x] = pixel.x;
            yvalues[width*y+x] = pixel.y;
            zvalues[width*y+x] = pixel.z;
        } 
    } 

    Laik_Partitioning* pValMaster = laik_new_partitioning(laik_Master, world, space, 0);
    laik_switchto_partitioning(xval, pValMaster, LAIK_DF_Preserve, LAIK_RO_Sum);
    laik_switchto_partitioning(yval, pValMaster, LAIK_DF_Preserve, LAIK_RO_Sum);
    laik_switchto_partitioning(zval, pValMaster, LAIK_DF_Preserve, LAIK_RO_Sum);

    if (laik_myid(world) == 0) {
        laik_map_def1(xval, (void**)&xvalues, 0);
        laik_map_def1(yval, (void**)&yvalues, 0);
        laik_map_def1(zval, (void**)&zvalues, 0);
        // Save result to a PPM image (keep these flags if you compile under Windows)
        std::ofstream ofs("./untitled.ppm", std::ios::out | std::ios::binary);
        ofs << "P6\n" << width << " " << height << "\n255\n";
        for (unsigned i = 0; i < width * height; ++i) {
            ofs << (unsigned char)(std::min(double(1), xvalues[i]) * 255) <<
                   (unsigned char)(std::min(double(1), yvalues[i]) * 255) <<
                   (unsigned char)(std::min(double(1), zvalues[i]) * 255);
        }
        ofs.close();
    }

    laik_finalize(inst);
    return 0;
}
