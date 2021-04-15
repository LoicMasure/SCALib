//! Estimation for higher-order T-test.
//!
//! An estimation of Ttest is represented with a Ttest struct. Calling update allows
//! to update the Ttest state with fresh measurements. get_ttest returns the current value
//! of the estimate.
//! The measurements are expected to be of length ns.
//!
//! This is based on the one-pass algorithm proposed in
//! https://eprint.iacr.org/2015/207

use ndarray::{s, Array1, Array2, Array3, Axis};
use num_integer::binomial;
use numpy::{PyArray2, PyReadonlyArray1, PyReadonlyArray2, ToPyArray};
use pyo3::prelude::*;
use rayon::prelude::*;

#[pyclass]
    /// Central sums of order 1 up to order d*2 with shape (2,ns,2*d),
    /// where central sums is sum((x-u_x)**i).
    /// Axes are (class, trace sample, order).
    /// cs[..,0,..] contains the current estimation of means instead of
    /// the central sum (which would be zero).
    /// number of samples in a trace
    ns: usize,
}
#[pymethods]
impl Ttest {
    #[new]
    /// Create a new Ttest state.
    /// ns: traces length
    /// d: order of the Ttest
    fn new(ns: usize, d: usize) -> Self {
        Ttest {
            cs: Array3::<f64>::zeros((2, ns, 2 * d)),
            n_samples: Array1::<u64>::zeros((2,)),
            d: d,
            ns: ns,
        }
    }
    /// Update the Ttest state with n fresh traces
    /// traces: the leakage traces with shape (n,ns)
    /// y: realization of random variables with shape (n,)
    fn update(&mut self, py: Python, traces: PyReadonlyArray2<i16>, y: PyReadonlyArray1<u16>) {
        let traces = traces.as_array();
        let y = y.as_array();
        let d = self.d;
        // pre computes the combinatorial factors
        let cbs: Vec<(usize, Vec<(f64, usize)>)> = (2..((2 * self.d) + 1))
            .rev()
            .map(|j| {
                (
                    j,
                    (1..(j - 1)).map(|k| (binomial(j, k) as f64, k)).collect(),
                )
            })
            .collect();

        py.allow_threads(|| {
            traces
                .outer_iter()
                .zip(y.outer_iter())
                    let y = *y.first().unwrap() as usize;
                    assert!(y <= 1);
                    let mut cs = self.cs.slice_mut(s![y, .., ..]);

                    // update the number of observations
                    let mut n = self.n_samples.slice_mut(s![*y as usize]);
                    n += 1;
                    let n = *n.first().unwrap() as f64;

                    //let mut delta_pows = Array1::<f64>::zeros(2 * self.d);

                    // compute the multiplicative factor similar for all trace samples
                    let mults: Vec<f64> = cbs
                        .iter()
                        .map(|(j, _)| {
                            (n - 1.0).powi(*j as i32)
                                * (1.0 - (-1.0 / (n - 1.0)).powi(*j as i32 - 1))
                        })
                        .collect();

                    // par iter on chuncks of size 20
                    (
                        cs.axis_chunks_iter_mut(Axis(0), 20),
                        traces.axis_chunks_iter(Axis(0), 20),
                    )
                        .into_par_iter()
                        .for_each_init(
                            || 
                                // array for powers of delta
                                Array1::<f64>::zeros(2 * d),
                            |ref mut delta_pows, (mut cs, traces)| {
                                cs.axis_iter_mut(Axis(0)).zip(traces.iter()).for_each(
                                    |(mut cs, traces)| {
                                        let cs = cs.as_slice_mut().unwrap();

                                        // compute the delta
                                        let delta = ((*traces as f64) - cs[0]) / (n as f64);

                                        // delta_pows[i] = delta ** (i+1)
                                        // We will need all of them next
                                        delta_pows.iter_mut().fold(delta, |acc, x| {
                                            *x = acc;
                                            acc * delta
                                        });

                                        // apply the one-pass update rule
                                        cbs.iter().zip(mults.iter()).for_each(
                                            |((j, vec), mult)| {
                                                if n > 1.0 {
                                                    cs[*j - 1] += delta_pows[*j - 1] * mult;
                                                }
                                                vec.iter().for_each(|(cb, k)| {
                                                    let a = cs[*j - *k - 1];
                                                    if (k & 0x1) == 1 {
                                                        // k is not pair
                                                        cs[*j - 1] -= cb * delta_pows[*k - 1] * a;
                                                    } else {
                                                        // k is pair
                                                        cs[*j - 1] += cb * delta_pows[*k - 1] * a;
                                                    }
                                                });
                                            },
                                        );
                                        cs[0] += delta;
                                    },
                                );
                            },
                        );
                });
        });
    }

    /// Generate the actual Ttest metric based on the current state.
    /// return array axes (d,ns)
    fn get_ttest<'py>(&mut self, py: Python<'py>) -> PyResult<&'py PyArray2<f64>> {
        let mut ttest = Array2::<f64>::zeros((self.d, self.ns));
        let cs = &self.cs;
        let n_samples = &self.n_samples;

        let n0 = n_samples[[0]] as f64;
        let n1 = n_samples[[1]] as f64;

        py.allow_threads(|| {
            (
                ttest.axis_chunks_iter_mut(Axis(1), 20),
                cs.axis_chunks_iter(Axis(1), 20),
            )
                .into_par_iter()
                .for_each(|(mut ttest, cs)| {
                    ttest
                        .axis_iter_mut(Axis(1))
                        .zip(cs.axis_iter(Axis(1)))
                        .for_each(|(mut ttest, cs)| {
                            let mut u0;
                            let mut u1;
                            let mut v0;
                            let mut v1;
                            for d in 1..(self.d + 1) {
                                if d == 1 {
                                    u0 = cs[[0, 0]];
                                    u1 = cs[[1, 0]];

                                    v0 = cs[[0, 1]] / n0;
                                    v1 = cs[[1, 1]] / n1;
                                } else if d == 2 {
                                    u0 = cs[[0, 1]] / n0;
                                    u1 = cs[[1, 1]] / n1;

                                    v0 = cs[[0, 3]] / n0 - ((cs[[0, 1]] / n0).powi(2));
                                    v1 = cs[[1, 3]] / n1 - ((cs[[1, 1]] / n1).powi(2));
                                } else {
                                    u0 = (cs[[0, d - 1]] / n0)
                                        / ((cs[[0, 1]] / n0).powf(d as f64 / 2.0));
                                    u1 = (cs[[1, d - 1]] / n1)
                                        / ((cs[[1, 1]] / n1).powf(d as f64 / 2.0));

                                    v0 =
                                        cs[[0, (2 * d) - 1]] / n0 - ((cs[[0, d - 1]] / n0).powi(2));
                                    v0 /= (cs[[0, 1]] / n0).powi(d as i32);

                                    v1 =
                                        cs[[1, (2 * d) - 1]] / n1 - ((cs[[1, d - 1]] / n1).powi(2));
                                    v1 /= (cs[[1, 1]] / n1).powi(d as i32);
                                }

                                ttest[d - 1] = (u0 - u1) / f64::sqrt((v0 / n0) + (v1 / n1));
                            }
                        });
                });
        });
        Ok(&(ttest.to_pyarray(py)))
    }
}
