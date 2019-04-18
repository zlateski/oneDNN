/*******************************************************************************
* Copyright 2017-2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef REF_SUM_HPP
#define REF_SUM_HPP

#include "reorder_pd.hpp"

#include "cpu_sum_pd.hpp"
#include "cpu_primitive.hpp"

namespace mkldnn {
namespace impl {
namespace cpu {

struct ref_sum_t: public cpu_primitive_t {
    struct pd_t: public cpu_sum_pd_t {
        using cpu_sum_pd_t::cpu_sum_pd_t;

        pd_t(const pd_t &rhs): cpu_sum_pd_t(rhs) {
            for (size_t i = 0; i < rhs.reorder_pds_.size(); ++i)
                reorder_pds_.push_back(
                       (const reorder_pd_t *)rhs.reorder_pds_[i]->clone());
        }

        ~pd_t() { for (auto &rpd: reorder_pds_) delete rpd; }

        DECLARE_SUM_PD_T("ref:any", ref_sum_t);

        status_t init() {
            bool ok = cpu_sum_pd_t::init() == status::success;
            if (!ok) return status::unimplemented;

            for (int i = 0; i < n_; ++i) {
                auto r_impls = engine_->get_reorder_implementation_list();
                for (auto r = r_impls; *r; ++r) {
                    primitive_attr_t attr;
                    attr.output_scales_.set(scales_[i]);
                    if (i != 0) attr.post_ops_.append_sum(1.0);

                    reorder_pd_t *r_pd;
                    if ((*r)(&r_pd, engine_, &attr, engine_, src_md(i),
                                engine_, dst_md()) == status::success) {
                        r_pd->init_info();
                        reorder_pds_.push_back(r_pd);
                        break;
                    }
                }
            }

            ok = reorder_pds_.size() == (size_t)n_;
            return ok ? status::success : status::unimplemented;
        }

        nstl::vector<const reorder_pd_t *> reorder_pds_;
    };

    ref_sum_t(const pd_t *apd): cpu_primitive_t(apd) {
        const int n = pd()->n_inputs();
        reorders_.resize(n);
        for (int i = 0; i < n; ++i)
            pd()->reorder_pds_[i]->create_primitive(&reorders_[i]);
    }

    ~ref_sum_t() { for (auto &r: reorders_) r->release(); }

    virtual status_t execute(const exec_ctx_t &ctx) const override {
        const auto n = pd()->n_inputs();
        for (int i = 0; i < n; ++i) {
            exec_args_t r_args;
            r_args[MKLDNN_ARG_SRC] = ctx.args().at(MKLDNN_ARG_MULTIPLE_SRC + i);
            r_args[MKLDNN_ARG_DST] = ctx.args().at(MKLDNN_ARG_DST);
            exec_ctx_t r_ctx(ctx, std::move(r_args));
            reorders_[i]->execute(r_ctx);
        }
        return status::success;
    }

private:
    const pd_t *pd() const { return (const pd_t *)primitive_t::pd(); }
    nstl::vector<primitive_t *> reorders_;
};

}
}
}

#endif
