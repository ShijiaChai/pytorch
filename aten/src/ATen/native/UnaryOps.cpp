// define constants like M_PI and C keywords for MSVC
#ifdef _MSC_VER
#define _USE_MATH_DEFINES
#include <math.h>
#endif

#include <ATen/ATen.h>
#include <ATen/Dispatch.h>
#include <ATen/ExpandUtils.h>
#include <ATen/NativeFunctions.h>
#include <ATen/LegacyTHFunctionsCPU.h>
#include <ATen/MemoryOverlap.h>
#include <ATen/WrapDimUtils.h>

#include <ATen/CPUApplyUtils.h>
#include <ATen/Parallel.h>
#include <ATen/native/UnaryOps.h>
#include <ATen/native/TensorIterator.h>
#ifdef BUILD_NAMEDTENSOR
#include <ATen/NamedTensorUtils.h>
#endif

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <vector>

#include <map>

namespace at {
namespace native {

Tensor logical_not(const Tensor& self) {
  Tensor result = at::empty({0}, self.options().dtype(kBool));
  return at::logical_not_out(result, self);
}

Tensor& logical_not_(Tensor& self) {
  return at::logical_not_out(self, self);
}

Tensor& logical_not_out(Tensor& result, const Tensor& self) {
  TensorIterator iter;
  iter.dont_compute_common_dtype();
  iter.check_and_add_output(result);
  iter.add_input(self);
  iter.build();
  logical_not_stub(iter.device_type(), iter);
  return result;
}

Tensor clamp(const Tensor& self, optional<Scalar> min, optional<Scalar> max) {
  Tensor result = at::empty({0}, self.options());
  return clamp_out(result, self, min, max);
}

Tensor clamp_max(const Tensor& self, Scalar max) {
  Tensor result = at::empty({0}, self.options());
  return clamp_max_out(result, self, max);
}

Tensor clamp_min(const Tensor& self, Scalar min) {
  Tensor result = at::empty({0}, self.options());
  return clamp_min_out(result, self, min);
}

Tensor& _clamp__cpu(Tensor& self, optional<Scalar> min, optional<Scalar> max) {
  return _clamp_out_cpu(self, self, min, max);
}

Tensor& _clamp_out_cpu(
    Tensor& result,
    const Tensor& self,
    optional<Scalar> min,
    optional<Scalar> max) {
  if (min && max) {
    legacy::cpu::_th_clamp_out(result, self, *min, *max);
  } else if (max) {
    legacy::cpu::_th_clamp_max_out(result, self, *max);
  } else if (min) {
    legacy::cpu::_th_clamp_min_out(result, self, *min);
  } else {
    AT_ERROR("At least one of 'min' or 'max' must not be None");
  }
#ifdef BUILD_NAMEDTENSOR
  at::namedinference::propagate_names(result, self);
#endif
  return result;
}

Tensor& _clamp_max__cpu(Tensor& self, Scalar max) {
  return legacy::cpu::_th_clamp_max_out(self, self, max);
}

Tensor& _clamp_max_out_cpu(Tensor& result, const Tensor& self, Scalar max) {
  legacy::cpu::_th_clamp_max_out(result, self, max);
#ifdef BUILD_NAMEDTENSOR
  at::namedinference::propagate_names(result, self);
#endif
  return result;
}

Tensor& _clamp_min__cpu(Tensor& self, Scalar min) {
  return legacy::cpu::_th_clamp_min_out(self, self, min);
}

Tensor& _clamp_min_out_cpu(Tensor& result, const Tensor& self, Scalar min) {
  legacy::cpu::_th_clamp_min_out(result, self, min);
#ifdef BUILD_NAMEDTENSOR
  at::namedinference::propagate_names(result, self);
#endif
  return result;
}

Tensor mvlgamma(const Tensor& self, int64_t p) {
  TORCH_CHECK(at::isFloatingType(self.scalar_type()),
           "mvlgamma is not implemented for ", self.type());
  TORCH_CHECK((self > 0.5 * (p - 1.)).all().item<uint8_t>(),
           "Condition for computing multivariate log-gamma not met");
  TORCH_CHECK(p >= 1, "p has to be greater than or equal to 1");
  Tensor args = native::arange(-p / 2. + 0.5, 0.5, 0.5, self.options());
  args = args.add(self.unsqueeze(-1));
  return args.lgamma_().sum(-1).add_(p * (p - 1) * std::log(M_PI) / 4.);
}

Tensor& mvlgamma_(Tensor& self, int64_t p) {
  TORCH_CHECK(at::isFloatingType(self.scalar_type()),
           "mvlgamma is not implemented for ", self.type());
  TORCH_CHECK((self > 0.5 * (p - 1.)).all().item<uint8_t>(),
           "Condition for computing multivariate log-gamma not met");
  TORCH_CHECK(p >= 1, "p has to be greater than or equal to 1");
  Tensor args = native::arange(-p / 2. + 0.5, 0.5, 0.5, self.options());
  args = args.add(self.unsqueeze(-1));
  return self.copy_(args.lgamma_().sum(-1).add_(p * (p - 1) * std::log(M_PI) / 4.));
}

inline void propagate_names_if_namedtensor_enabled(Tensor& result, const Tensor& src) {
#ifdef BUILD_NAMEDTENSOR
  at::namedinference::propagate_names(result, src);
#endif
}

// NOTE:
// YOU ARE NOT OBLIGED TO USE THESE HELPERS
// If you're writing something more specialized, please don't try to make them
// work for your case, but just write something new instead.

// A helper class that reduces redundant code in implementing the most typical kind of unary operators. This allows some
// preprocessing that are unique to some operators (more is forsee-able in the future) and is more flexible and elegant
// than defining a flat fat macro that implements everything.
template <typename Stub>
class TypicalUnaryOpImpl {
private:
  typename std::remove_reference<Stub>::type stub;

public:
  TypicalUnaryOpImpl(Stub&& stub) : stub(std::forward<Stub>(stub)){}

  inline Tensor& unary_op_out_impl(Tensor& result, const Tensor& self) {
    auto iter = TensorIterator::unary_op(result, self,
      /*check_internal_overlap=*/true);
    this->stub(iter.device_type(), iter);
    return result;
  }

  inline Tensor unary_op_impl(const Tensor& self) {
    Tensor result = at::empty({0}, self.options());
    return this->unary_op_out_impl(result, self);
  }

  inline Tensor& unary_op_impl_(Tensor& self) {
    return this->unary_op_out_impl(self, self);
  }
};

// A factory function that circumvents the unavailability of class template type deduction in C++ 11.
template <typename Stub>
static inline auto create_typical_unary_op_impl(Stub stub) -> TypicalUnaryOpImpl<Stub> {
  return TypicalUnaryOpImpl<Stub>(std::forward<Stub>(stub));
}

static auto neg_op = create_typical_unary_op_impl(neg_stub);
Tensor neg(const Tensor& self) { return neg_op.unary_op_impl(self); }
Tensor& neg_(Tensor& self) { return neg_op.unary_op_impl_(self); }
Tensor& neg_out(Tensor& result, const Tensor& self) {
  TORCH_CHECK(self.scalar_type() != kBool,
              "Negation, the `-` operator, on a bool tensor is not supported. "
              "If you are trying to invert a mask, use the `~` or `logical_not()` operator instead.");
  return neg_op.unary_op_out_impl(result, self);
}

#define IMPLEMENT_UNARY_OP(opr)  \
  static auto opr##_op = create_typical_unary_op_impl(opr##_stub); \
  Tensor& opr##_out(Tensor& result, const Tensor& self) { return opr##_op.unary_op_out_impl(result, self); } \
  Tensor opr(const Tensor& self) { return opr##_op.unary_op_impl(self); } \
  Tensor& opr##_(Tensor& self) { return opr##_op.unary_op_impl_(self); }

IMPLEMENT_UNARY_OP(bitwise_not);
IMPLEMENT_UNARY_OP(ceil);

// NB: If you use this macro, you may also need to add a CUDA forwarding
// stub in CUDAUnaryOps

#define IMPLEMENT_UNARY_OP_VEC(op)                              \
  Tensor op(const Tensor& self) {                               \
    Tensor result = at::empty({0}, self.options());             \
    at::op##_out(result, self);                                 \
    return result;                                              \
  }                                                             \
  Tensor& _##op##__cpu(Tensor& self) {                          \
    return at::op##_out(self, self);                            \
  }                                                             \
  Tensor& _##op##_out_cpu(Tensor& result, const Tensor& self) { \
    checkBackend(#op, result, Backend::CPU);                    \
    auto iter = TensorIterator::unary_op(result, self,          \
      /*check_internal_overlap=*/true);                         \
    op##_stub(iter.device_type(), iter);                        \
    return result;                                              \
  }

IMPLEMENT_UNARY_OP_VEC(abs)
IMPLEMENT_UNARY_OP_VEC(acos)
IMPLEMENT_UNARY_OP_VEC(asin)
IMPLEMENT_UNARY_OP_VEC(atan)
IMPLEMENT_UNARY_OP_VEC(cos)
IMPLEMENT_UNARY_OP_VEC(cosh)
IMPLEMENT_UNARY_OP_VEC(erf)
IMPLEMENT_UNARY_OP_VEC(erfc)
IMPLEMENT_UNARY_OP_VEC(exp)
IMPLEMENT_UNARY_OP_VEC(expm1)
IMPLEMENT_UNARY_OP_VEC(floor)
IMPLEMENT_UNARY_OP_VEC(frac)
IMPLEMENT_UNARY_OP_VEC(log)
IMPLEMENT_UNARY_OP_VEC(log10)
IMPLEMENT_UNARY_OP_VEC(log1p)
IMPLEMENT_UNARY_OP_VEC(log2)
IMPLEMENT_UNARY_OP_VEC(reciprocal)
IMPLEMENT_UNARY_OP_VEC(round)
IMPLEMENT_UNARY_OP_VEC(rsqrt)
IMPLEMENT_UNARY_OP_VEC(sigmoid)
IMPLEMENT_UNARY_OP_VEC(sin)
IMPLEMENT_UNARY_OP_VEC(sinh)
IMPLEMENT_UNARY_OP_VEC(sqrt)
IMPLEMENT_UNARY_OP_VEC(tan)
IMPLEMENT_UNARY_OP_VEC(tanh)
IMPLEMENT_UNARY_OP_VEC(trunc)

DEFINE_DISPATCH(abs_stub);
DEFINE_DISPATCH(acos_stub);
DEFINE_DISPATCH(asin_stub);
DEFINE_DISPATCH(atan_stub);
DEFINE_DISPATCH(bitwise_not_stub);
DEFINE_DISPATCH(ceil_stub);
DEFINE_DISPATCH(cos_stub);
DEFINE_DISPATCH(cosh_stub);
DEFINE_DISPATCH(erf_stub);
DEFINE_DISPATCH(erfc_stub);
DEFINE_DISPATCH(exp_stub);
DEFINE_DISPATCH(expm1_stub);
DEFINE_DISPATCH(floor_stub);
DEFINE_DISPATCH(frac_stub);
DEFINE_DISPATCH(log_stub);
DEFINE_DISPATCH(log10_stub);
DEFINE_DISPATCH(log1p_stub);
DEFINE_DISPATCH(log2_stub);
DEFINE_DISPATCH(logical_not_stub);
DEFINE_DISPATCH(neg_stub);
DEFINE_DISPATCH(reciprocal_stub);
DEFINE_DISPATCH(round_stub);
DEFINE_DISPATCH(rsqrt_stub);
DEFINE_DISPATCH(sigmoid_stub);
DEFINE_DISPATCH(sin_stub);
DEFINE_DISPATCH(sinh_stub);
DEFINE_DISPATCH(sqrt_stub);
DEFINE_DISPATCH(tan_stub);
DEFINE_DISPATCH(tanh_stub);
DEFINE_DISPATCH(trunc_stub);
}
} // namespace at
