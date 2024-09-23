/**
* @file mtx.hpp
* @brief Provides matrix operations and transformations for Wayland output handling.
*/

#pragma once
#include <cstring>
#include <iterator>
#include <algorithm>
#include <wayland-client.h>
#include "../helpers/Box.hpp"

/**
* @brief Inverts the given Wayland output transform.
* 
* @param tr Wayland output transform to be inverted.
* @return Inverted Wayland output transform.
*/
inline wl_output_transform invertOutputTransform(wl_output_transform tr) {
    if ((tr & WL_OUTPUT_TRANSFORM_90) && !(tr & WL_OUTPUT_TRANSFORM_FLIPPED))
        tr = static_cast<wl_output_transform>(tr ^ WL_OUTPUT_TRANSFORM_180);

    return tr;
}

/**
* @brief Sets a 3x3 matrix to the identity matrix.
* 
* @param mat A 3x3 matrix (array of 9 floats).
*/
inline void matrixIdentity(float mat[9]) {
    const float identity[9] = {
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
    std::copy(std::begin(identity), std::end(identity), mat);
}

/**
* @brief Multiplies two 3x3 matrices.
* 
* @param result The resulting matrix (3x3).
* @param a First matrix (3x3).
* @param b Second matrix (3x3).
*/
inline void matrixMultiply(float result[9], const float a[9], const float b[9]) {
    float product[9];

    product[0] = a[0] * b[0] + a[1] * b[3] + a[2] * b[6];
    product[1] = a[0] * b[1] + a[1] * b[4] + a[2] * b[7];
    product[2] = a[0] * b[2] + a[1] * b[5] + a[2] * b[8];

    product[3] = a[3] * b[0] + a[4] * b[3] + a[5] * b[6];
    product[4] = a[3] * b[1] + a[4] * b[4] + a[5] * b[7];
    product[5] = a[3] * b[2] + a[4] * b[5] + a[5] * b[8];

    product[6] = a[6] * b[0] + a[7] * b[3] + a[8] * b[6];
    product[7] = a[6] * b[1] + a[7] * b[4] + a[8] * b[7];
    product[8] = a[6] * b[2] + a[7] * b[5] + a[8] * b[8];

    std::copy(std::begin(product), std::end(product), result);
}

/**
* @brief Transposes a 3x3 matrix.
* 
* @param result The resulting transposed matrix (3x3).
* @param src The source matrix (3x3).
*/
inline void matrixTranspose(float result[9], const float src[9]) {
    float transposed[9] = {
        src[0], src[3], src[6], src[1], src[4], src[7], src[2], src[5], src[8],
    };
    std::copy(std::begin(transposed), std::end(transposed), result);
}

/**
* @brief Applies translation to a 3x3 matrix.
* 
* @param mat The matrix to translate.
* @param x Translation along the x-axis.
* @param y Translation along the y-axis.
*/
inline void matrixTranslate(float mat[9], float x, float y) {
    float translate[9] = {
        1.0f, 0.0f, x, 0.0f, 1.0f, y, 0.0f, 0.0f, 1.0f,
    };
    matrixMultiply(mat, mat, translate);
}

inline void matrixScale(float mat[9], float x, float y) {
    float scale[9] = {
        x, 0.0f, 0.0f, 0.0f, y, 0.0f, 0.0f, 0.0f, 1.0f,
    };
    matrixMultiply(mat, mat, scale);
}

/**
* @brief Rotates a 3x3 matrix by the given radians.
* 
* @param mat The matrix to rotate.
* @param rad Rotation angle in radians.
*/
inline void matrixRotate(float mat[9], float rad) {
    float rotate[9] = {
        cos(rad), -sin(rad), 0.0f, sin(rad), cos(rad), 0.0f, 0.0f, 0.0f, 1.0f,
    };
    matrixMultiply(mat, mat, rotate);
}

// Transformation matrices
constexpr float transforms[][9] = {
    // WL_OUTPUT_TRANSFORM_NORMAL
    {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
    // WL_OUTPUT_TRANSFORM_90
    {0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
    // WL_OUTPUT_TRANSFORM_180
    {-1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
    // WL_OUTPUT_TRANSFORM_270
    {0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
    // WL_OUTPUT_TRANSFORM_FLIPPED
    {-1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
    // WL_OUTPUT_TRANSFORM_FLIPPED_90
    {0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
    // WL_OUTPUT_TRANSFORM_FLIPPED_180
    {1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
    // WL_OUTPUT_TRANSFORM_FLIPPED_270
    {0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};

/**
* @brief Applies a given Wayland output transformation to a 3x3 matrix.
* 
* @param mat The matrix to transform.
* @param transform The Wayland output transform to apply.
*/
inline void applyMatrixTransform(float mat[9], wl_output_transform transform) {
    matrixMultiply(mat, mat, transforms[transform]);
}

/**
* @brief Computes a projection matrix for a given width, height, and Wayland output transform.
* 
* @param mat The resulting projection matrix (3x3).
* @param width Width of the output.
* @param height Height of the output.
* @param transform Wayland output transform to apply.
*/
inline void matrixProjection(float mat[9], int width, int height, wl_output_transform transform) {
    std::fill_n(mat, 9, 0.0f);

    const float* t = transforms[transform];
    float        x = 2.0f / width;
    float        y = -2.0f / height;

    // Rotation + reflection
    mat[0] = x * t[0];
    mat[1] = x * t[1];
    mat[3] = y * -t[3];
    mat[4] = y * -t[4];

    // Translation
    mat[2] = -std::copysign(1.0f, mat[0] + mat[1]);
    mat[5] = -std::copysign(1.0f, mat[3] + mat[4]);

    // Identity
    mat[8] = 1.0f;
}

/**
* @brief Projects a box using a given transformation and projection matrix.
* 
* @param mat The resulting matrix after applying the box projection.
* @param box The box to project (CBox structure).
* @param transform Wayland output transform.
* @param rotation Rotation angle in radians.
* @param projection The projection matrix (3x3).
*/
inline void matrixProjectBox(float mat[9], const CBox* box, wl_output_transform transform, float rotation, const float projection[9]) {
    int x      = box->x;
    int y      = box->y;
    int width  = box->width;
    int height = box->height;

    matrixIdentity(mat);
    matrixTranslate(mat, x, y);

    if (rotation != 0) {
        matrixTranslate(mat, width / 2.0f, height / 2.0f);
        matrixRotate(mat, rotation);
        matrixTranslate(mat, -width / 2.0f, -height / 2.0f);
    }

    matrixScale(mat, width, height);

    if (transform != WL_OUTPUT_TRANSFORM_NORMAL) {
        matrixTranslate(mat, 0.5f, 0.5f);
        applyMatrixTransform(mat, transform);
        matrixTranslate(mat, -0.5f, -0.5f);
    }

    matrixMultiply(mat, projection, mat);
}
