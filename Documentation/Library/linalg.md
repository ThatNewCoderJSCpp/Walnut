# linalg

`linalg` is linear algebra: vectors, matrices, quaternions and octonions, with determinants, inverses, decompositions, eigenvalues and eigenvectors, and the transforms used in graphics.

```walnut
import module.linalg
```

Everything is in the namespace `std::linalg`. Every type takes its element type as the template parameter `T`, and `T` has to be a real float type, such as `float`, `short float`, `double` or `std::math::BigFloat` (or a type of your own that [counts as a float](core.md#extending-a-trait)). An integer type is an error when the type is used:

```
std::linalg::Vector3<int>: v;
```

```
Assertion Error: static assertion failed: linalg vectors need a real float type, such as float or double, not an integer or other type
```

## A first example

```walnut
import module.io
import module.linalg

using std::linalg::Vector3;
using std::linalg::Matrix3;

function main() -> int {
    Vector3<float>: a(1.0, 2.0, 3.0);
    Vector3<float>: b(4.0, 5.0, 6.0);
    std::io::println(a + b, " ", a.dot(b), " ", a.cross(b), " ", 2.0 * a);

    Matrix3<float>: m(2, -1, 0, -1, 2, -1, 0, -1, 2);
    std::io::println(m.determinant(), " ", m.rank(), " ", m.trace());
    auto: e = m.symmetric_eigen();
    std::io::println(e.values[0] < e.values[1], " ", (m.to_dyn() * e.vector(0) - e.vector(0) * e.values[0]).norm_inf() < 1.0e-20);
    return 0;
} end function main
```

This prints `(5, 7, 9) 32 (-3, 6, -3) (2, 4, 6)`, then `4 3 6`, then `1 1`.

## The types

| Type | What it is |
|---|---|
| `Vector2<T>`, `Vector3<T>`, `Vector4<T>` | Small vectors with the fields `x`, `y`, `z` and `w` |
| `VectorN<T, N>` | A vector of `N` components, where `N` is known when compiling |
| `DynVector<T>` | A vector whose size is chosen at run time |
| `Matrix<T, R, C>` | An `R`-by-`C` matrix, where `R` and `C` are known when compiling |
| `SquareMatrix<T, N>` | `Matrix<T, N, N>` |
| `Matrix2<T>`, `Matrix3<T>`, `Matrix4<T>` | `Matrix<T, 2, 2>`, `Matrix<T, 3, 3>`, `Matrix<T, 4, 4>` |
| `Matrix2x3<T>`, `Matrix3x2<T>`, `Matrix3x4<T>`, `Matrix4x3<T>` | The rectangular sizes with those names |
| `DynMatrix<T>` | A matrix whose size is chosen at run time |
| `KVector<T, K, N>` | A k-vector: a grade-`K` element of the exterior algebra of `N` dimensions, with C(N, K) components |
| `Bivector<T, N>`, `Trivector<T, N>`, `Pseudoscalar<T, N>` | `KVector<T, 2, N>`, `KVector<T, 3, N>` and `KVector<T, N, N>` |
| `Quaternion<T>` | A quaternion `w + xi + yj + zk`, used for 3D rotations |
| `Octonion<T>` | An octonion, with the eight components `e[0]` to `e[7]` |
| `LUDecomposition<T>`, `QRDecomposition<T>`, `SymmetricEigen<T>`, `SingularValueDecomposition<T>` | Results of the decompositions below |
| `AxisAngle<T>` | An axis and an angle, from `Quaternion::to_axis_angle()` |

Mistakes that can only be found while the program runs, such as a `DynMatrix` product with mismatched sizes, an index out of range, or the inverse of a singular matrix, throw a `std::errors::linalg_error`. Its `what()` says what went wrong:

```walnut
import module.io
import module.linalg

function main() -> int {
    try {
        std::linalg::Matrix<float, 2, 2>: s(1, 2, 2, 4);
        auto: bad = s.inverse();
    } catch (const std::errors::linalg_error&: err) {
        std::io::println("error: ", err.what());
    }
    return 0;
} end function main
```

This prints `error: inverse() of a singular matrix: its determinant is 0`.

## Vectors

`Vector2`, `Vector3` and `Vector4` are made from their components, `Vector3<float>(1.0, 2.0, 3.0)`; with no arguments every component is 0. `VectorN<T, N>` is made from exactly `N` values, `VectorN<float, 4>(1, 2, 3, 4)`, or from an `array[N] T`. `DynVector<T>(n)` has `n` zeros, `DynVector<T>(n, value)` has `n` copies of `value`, and `DynVector<T>(values)` copies a `std::collections::Vector<T>`.

**Making one**

| Written | Gives |
|---|---|
| `V::zero()`, `V::one()`, `V::filled(value)` | All 0, all 1, or all `value` (`DynVector` takes the size first: `DynVector<T>::zero(n)`) |
| `Vector3<T>::unit_x()`, `unit_y()`, `unit_z()`, and `unit_w()` on `Vector4` | A unit vector along one axis |
| `VectorN<T, N>::unit(axis)`, `DynVector<T>::unit(n, axis)` | A unit vector along axis number `axis` |
| `Vector2<T>::from_angle(radians)` | `(cos, sin)` of the angle |

**Operators.** `+`, `-`, `*` and `/` work between two vectors of the same type, component by component. `*` and `/` also take a scalar, on either side for `*`: `v * 2.0`, `2.0 * v`. Unary `-`, `+=`, `-=`, `*=`, `/=`, `==` and `!=` work as expected. `v[i]` and `v.at(i)` reach component `i`, and an index out of range throws.

**Methods** (on every vector type)

| Method | Gives |
|---|---|
| `dot(o)` | The dot product |
| `length()`, `magnitude()`, `norm()` | The Euclidean length |
| `length_squared()` | The length squared, which skips the square root |
| `norm_1()`, `norm_inf()`, `norm_p(p)` | The sum of absolute values, the largest absolute value, and the p-norm (`VectorN` and `DynVector`) |
| `normalized()`, `normalize()` | A copy with length 1, or the same change made in place. A zero vector stays zero |
| `is_normalized()`, `is_zero()` | Whether the length is 1, or every component is 0 |
| `distance(o)`, `distance_squared(o)` | The distance to another point |
| `lerp(o, t)` | The point `t` of the way from this one to `o` |
| `angle_to(o)` | The angle between the two, in radians |
| `project_onto(o)`, `reject_from(o)` | The part along `o`, and the part at right angles to it |
| `reflect(normal)` | The reflection off a surface with that unit normal |
| `min(o)`, `max(o)`, `abs()`, `clamp(lo, hi)` | Component by component |
| `sum()`, `product()`, `min_component()`, `max_component()` | Over the components; `mean()`, `argmin()` and `argmax()` on `VectorN` and `DynVector` |
| `approx_equal(o)`, `approx_equal(o, tolerance)` | Whether every component is within a small tolerance |
| `to_string()` | Text such as `(1, 2, 3)` |

**Extra methods by type**

| Type | Methods |
|---|---|
| `Vector2` | `cross(o)` and `perp_dot(o)` (the 2D cross product, a scalar), `perpendicular()`, `angle()`, `rotated(radians)`, `extend(z)` (gives a `Vector3`) |
| `Vector3` | `cross(o)`, `triple(b, c)` (the scalar triple product), `rotated(axis, radians)`, `any_perpendicular()`, `xy()`, `extend(w)` (gives a `Vector4`) |
| `Vector4` | `xyz()`, `xy()`, `project()` (divides by `w`) |
| `VectorN` | `cross(o)` when `N` is 3, `outer(o)` (gives a `Matrix<T, N, N>`), `to_dyn()`, `VectorN::from_dyn(d)`, `begin()`/`end()` for `for` loops |
| `DynVector` | `size()`, `empty()`, `push_back(x)`, `resize(n)`, `values()`, `cross(o)` for size 3, `outer(o)` (gives a `DynMatrix`), `begin()`/`end()` |

`Vector2`, `Vector3` and `Vector4` also have `to_vector()` (a `VectorN`) and `to_dyn()` (a `DynVector`).

## Matrices

`Matrix<T, R, C>` with no arguments is all zeros. It can be made from `R * C` values, row by row, from an `array[R * C] T`, or with `from_rows`, `from_columns`, `diagonal`, `identity`, `zero` or `filled`. `m(r, c)` and `m.at(r, c)` reach the element in row `r` and column `c`; both are checked.

```walnut
import module.io
import module.linalg

using std::linalg::Matrix;

function main() -> int {
    Matrix<float, 2, 3>: a(1, 2, 3, 4, 5, 6);
    auto: product = a * a.transpose();
    std::io::println(product, " ", product.determinant());
    std::io::println(product.inverse());
    std::io::println(a.rank(), " ", a.row(1), " ", a.column(2));
    return 0;
} end function main
```

`product` is the 2-by-2 matrix `[[14, 32], [32, 77]]`, with determinant `54`. `rank()` is `2`, `row(1)` is `(4, 5, 6)` and `column(2)` is `(3, 6)`.

`DynMatrix<T>(rows, cols)` is the same with sizes chosen at run time, and `DynMatrix<T>(rows, cols, value)` fills it. It is also made with `identity(n)`, `zero(rows, cols)`, `filled(rows, cols, value)`, `diagonal(values)`, `from_rows(list)`, `from_columns(list)` and `from_values(rows, cols, values)`. A `Matrix` converts with `to_dyn()`, and back with `Matrix<T, R, C>::from_dyn(d)`, which checks the size.

**Operators.** `+`, `-` and unary `-` work between matrices of the same size. `*` with a scalar works on either side, and `/` divides by one. `*` with another matrix is the matrix product: a `Matrix<T, R, C>` times a `Matrix<T, C, K>` gives a `Matrix<T, R, K>`, so a product of the wrong sizes doesn't compile. A matrix times a vector of the right size gives a vector, and this includes `Matrix2 * Vector2`, `Matrix3 * Vector3` and `Matrix4 * Vector4`. `+=`, `-=`, `*=`, `/=`, `==` and `!=` work too.

**Methods.** These are on both `Matrix` and `DynMatrix`. Some need a square matrix; on a `Matrix` that is checked when compiling, and on a `DynMatrix` it throws.

| Method | Gives |
|---|---|
| `rows()`, `cols()`, `size()`, `is_square()` | The shape |
| `row(i)`, `column(j)`, `set_row(i, v)`, `set_column(j, v)`, `swap_rows(a, b)`, `swap_columns(a, b)` | Rows and columns |
| `transpose()` | The transpose |
| `trace()` | The sum of the diagonal |
| `determinant()` | The determinant. Up to 3-by-3 it uses the formula; above that, LU with partial pivoting |
| `inverse()` | The inverse. Throws when the matrix is singular |
| `is_invertible()`, `is_singular()` | Whether it has an inverse |
| `rank()` | The rank |
| `solve(b)` | The `x` with `m * x == b`, for a vector `b` (or a `DynMatrix` of right-hand sides on a `DynMatrix`) |
| `least_squares(b)` | The `x` that makes `m * x` closest to `b`, for a matrix with more rows than columns |
| `power(n)` | `m` multiplied by itself `n` times; a negative `n` uses the inverse |
| `minor(r, c)`, `cofactor(r, c)`, `adjugate()` | The minor, cofactor and adjugate. `DynMatrix` also has `cofactor_matrix()` and `without(r, c)` |
| `hadamard(o)` | The product element by element. `DynMatrix` also has `kronecker(o)` |
| `frobenius_norm()`, `norm()`, `norm_1()`, `norm_inf()`, `norm_2()`, `max_abs()` | Matrix norms (`norm_2` is the largest singular value) |
| `condition_number()` | The ratio of the largest to the smallest singular value |
| `is_symmetric()`, `is_diagonal()`, `is_identity()`, `is_upper_triangular()`, `is_lower_triangular()`, `is_orthogonal()`, `is_positive_definite()` | Tests of the matrix's shape |
| `approx_equal(o)`, `approx_equal(o, tolerance)` | Whether every element is within a tolerance |
| `to_string()` | Text such as `[[1, 2], [3, 4]]` |

**Decompositions and eigenvalues**

| Method | Gives |
|---|---|
| `lu()` | An `LUDecomposition<T>` with `lower`, `upper`, `permutation`, `sign` and `singular`, and the methods `determinant()`, `solve(b)`, `inverse()` and `permutation_matrix()`. `permutation_matrix() * m == lower * upper` |
| `qr()` | A `QRDecomposition<T>` with `q` (orthogonal) and `r` (upper triangular), by Householder reflections. `q * r == m` |
| `cholesky()` | The lower triangular `l` with `l * l.transpose() == m`, for a symmetric positive-definite matrix |
| `svd()` | A `SingularValueDecomposition<T>` with `u`, `singular_values` (largest first) and `v` |
| `singular_values()` | The singular values, largest first |
| `pseudo_inverse()` | The Moore-Penrose pseudo-inverse; a `Matrix<T, R, C>` gives a `Matrix<T, C, R>` |
| `null_space()` | A `DynMatrix` whose columns span the null space (`DynMatrix` only) |
| `row_echelon()` | The reduced row echelon form (`DynMatrix` only) |
| `symmetric_eigen()` | For a symmetric matrix: a `SymmetricEigen<T>` with `values` (smallest first) and `vectors` (one unit eigenvector per column, also reached with `vector(i)`), by the Jacobi method |
| `eigenvalues()` | Every eigenvalue of any square matrix, as a `std::collections::Vector<std::math::Complex<T>>` sorted by real part, by Hessenberg reduction and the shifted QR algorithm |
| `real_eigenvalues()` | Just the real ones, as a `DynVector<T>` |
| `eigenvector(value)` | A unit eigenvector for that eigenvalue, by inverse iteration |
| `eigenvectors()` | A `DynMatrix` with one unit eigenvector per column, one for each real eigenvalue |

```walnut
import module.io
import module.linalg

function main() -> int {
    std::linalg::Matrix3<float>: rotation(0, -1, 0, 1, 0, 0, 0, 0, 1);
    auto: values = rotation.eigenvalues();
    for (long int: i = 0; i < values.size(); i++) {
        std::io::println(values[i].real(), " ", values[i].imaginary());
    }
    return 0;
} end function main
```

A quarter turn about the z axis has the eigenvalues `-i`, `i` and `1`, so this prints `0 -1`, `0 1` and `1 0`.

## Transforms

These free functions make the matrices used in 2D and 3D graphics. The 4-by-4 ones work on column vectors, `m * v`.

| Function | Gives |
|---|---|
| `rotation2(radians)` | A 2D rotation as a `Matrix<T, 2, 2>` |
| `rotation3(axis, radians)` | A rotation about `axis` as a `Matrix<T, 3, 3>` |
| `rotation(axis, radians)`, `rotation_x(radians)`, `rotation_y(radians)`, `rotation_z(radians)` | 4-by-4 rotations |
| `translation(offset)` | A 4-by-4 translation by a `Vector3` |
| `scaling(factors)`, `scaling(factor)` | A 4-by-4 scale |
| `look_at(eye, target, up)` | A view matrix, as in OpenGL |
| `perspective(fov_y, aspect, near_plane, far_plane)` | A perspective projection |
| `orthographic(left, right, bottom, top, near_plane, far_plane)` | An orthographic projection |
| `transform_point(m, p)`, `transform_direction(m, d)` | A `Vector3` moved by a 4-by-4 matrix, as a point (with `w = 1`, then divided by `w`) or a direction (with `w = 0`) |

When `T` can't be worked out from a plain number, give it: `std::linalg::rotation2<float>(0.5)`.

## K-vectors

A `KVector<T, K, N>` has one component for each set of `K` increasing axis indices out of `N`, in lexicographic order. A bivector in 3D, `KVector<T, 2, 3>`, has the components e01, e02 and e12. A `KVector<T, 1, N>` is an ordinary vector, a `KVector<T, 0, N>` is a scalar, and a `KVector<T, N, N>` is a pseudoscalar.

It is made with no arguments (all zeros), from its C(N, K) components, from an array, or with `zero()`, `unit(component)`, `from_vector(v)` (for `K` = 1) and `of_scalar(x)` (for `K` = 0).

| Method | Gives |
|---|---|
| `size()`, `grade()`, `dimension()` | C(N, K), `K` and `N` |
| `kv[i]`, `component(i)` | Component `i`, checked |
| `at(i, j, ...)` | The component for those `K` axis indices, which have to increase |
| `indices_of(i)`, `component_of(indices)` | Convert between a component number and its axis indices |
| `wedge(o)` | The exterior (wedge) product with a `KVector<T, J, N>`, a `KVector<T, K + J, N>` |
| `hodge()` | The Hodge dual, a `KVector<T, N - K, N>` |
| `dot(o)`, `norm()`, `norm_squared()`, `magnitude()`, `normalized()`, `is_zero()`, `approx_equal(o)` | Sizes, with the Euclidean metric |
| `scalar()`, `to_vector()` | The single value of a 0-vector or pseudoscalar, and a 1-vector as a `VectorN` |
| `to_string()` | Text such as `-3 e01 + -6 e02 + -3 e12` |

`+`, `-`, unary `-`, `*` and `/` by a scalar (on either side for `*`), the compound forms, `==` and `!=` work as for vectors. The free functions `wedge(a, b)` take two k-vectors, two `VectorN`s, or two `Vector2`, `Vector3` or `Vector4`s, and `hodge(v)` is the method as a function.

```walnut
import module.io
import module.linalg

using std::linalg::Vector3;

function main() -> int {
    Vector3<float>: a(1.0, 2.0, 3.0);
    Vector3<float>: b(4.0, 5.0, 6.0);
    auto: plane = std::linalg::wedge(a, b);
    std::io::println(plane);
    std::io::println(plane.hodge(), " and ", a.cross(b));
    return 0;
} end function main
```

This prints `-3 e01 + -6 e02 + -3 e12`, then `-3 e0 + 6 e1 + -3 e2 and (-3, 6, -3)`: in 3D the Hodge dual of a wedge product is the cross product.

## Quaternions

A `Quaternion<T>` has the fields `w`, `x`, `y` and `z`. With no arguments it is the identity `1 + 0i + 0j + 0k`. It is also made from its four components, from a scalar and a `Vector3`, or with:

| Written | Gives |
|---|---|
| `Quaternion<T>::identity()`, `zero()` | The identity, or all zeros |
| `from_axis_angle(axis, radians)` | A rotation about `axis` |
| `from_euler(roll, pitch, yaw)`, `from_euler(angles)` | A rotation from Euler angles (x, then y, then z) |
| `from_matrix(m)` | The rotation in a `Matrix3` |
| `from_to(a, b)` | The shortest rotation that turns direction `a` into direction `b` |
| `look_rotation(forward, up)` | A rotation that points along `forward` |

`+`, `-`, `*` (the Hamilton product), scalar `*` and `/`, `/` by another quaternion, `==`, `!=` and the compound forms all work. `q * v` with a `Vector3` rotates it.

| Method | Gives |
|---|---|
| `rotate(v)` | `v` rotated |
| `conjugate()`, `inverse()`, `normalized()`, `normalize()` | The usual operations |
| `norm()`, `length()`, `norm_squared()`, `dot(o)`, `is_normalized()` | Sizes |
| `scalar_part()`, `vector_part()` | `w`, and `(x, y, z)` as a `Vector3` |
| `angle()`, `axis()`, `to_axis_angle()` | The rotation as an angle and axis |
| `to_euler()` | `(roll, pitch, yaw)` as a `Vector3` |
| `to_matrix3()`, `to_matrix4()` | The rotation as a matrix |
| `slerp(o, t)`, `nlerp(o, t)` | Spherical and normalized linear interpolation, taking the shorter way round |
| `exp()`, `log()`, `pow(p)` | The quaternion exponential, logarithm and power |
| `angle_to(o)` | The angle of the rotation between the two |
| `approx_equal(o)`, `same_rotation(o)` | Equality within a tolerance; `same_rotation` also accepts `-o`, which is the same rotation |
| `to_vector()`, `to_string()` | `(x, y, z, w)` as a `Vector4`, and text such as `(1 + 0i + 0j + 0k)` |

```walnut
import module.io
import module.linalg

using std::linalg::Quaternion;
using std::linalg::Vector3;

function main() -> int {
    auto: quarter = Quaternion<float>::from_axis_angle(Vector3<float>::unit_z(), std::linalg::detail::pi<float>() / 2.0);
    std::io::println(quarter.rotate(Vector3<float>(1, 0, 0)).approx_equal(Vector3<float>(0, 1, 0)));
    std::io::println(Quaternion<float>::identity().slerp(quarter, 0.5).angle() * 4.0);
    return 0;
} end function main
```

This prints `1`, then pi (a half-way rotation is an eighth of a turn).

## Octonions

An `Octonion<T>` holds the eight components `e[0]` to `e[7]`, also reached with `o[i]`. It is made from eight values, from an `array[8] T`, or from two quaternions (the Cayley-Dickson halves), and with `zero()`, `one()`, `unit(i)` and `real_value(x)`. Multiplication follows the Cayley-Dickson construction: it is neither commutative nor associative, but the norm is multiplicative, so `(a * b).norm()` equals `a.norm() * b.norm()`.

| Method | Gives |
|---|---|
| `real()`, `imaginary()` | `e[0]`, and the other seven as a `VectorN<T, 7>` |
| `first_half()`, `second_half()` | The two quaternion halves |
| `conjugate()`, `inverse()`, `normalized()` | The usual operations; `a / b` is `a * b.inverse()` |
| `norm()`, `norm_squared()`, `dot(o)` | Sizes |
| `approx_equal(o)`, `to_string()` | As for the other types |

## Free functions

For code that reads better as functions, `dot`, `length`, `length_squared`, `distance`, `normalize`, `lerp`, `angle_between`, `project` and `reflect` take every vector type; `cross(a, b)` takes `Vector2`, `Vector3`, `VectorN` and `DynVector`; `triple_product(a, b, c)` and `outer(a, b)` work as their methods do. `transpose`, `determinant`, `inverse`, `trace`, `rank`, `eigenvalues` and `eigenvectors` take a `Matrix` or a `DynMatrix`, and `dot`, `slerp` and `nlerp` take quaternions.
