#pragma once

#include <unordered_map>
#include "kvstorage.h"
#include "op.h"
#include "tensor.h"
#include "kern/kernel_define.h"

namespace inferllm {

//! TODO: redefine this member of the struct
struct LlmParams {
    bool is_multi_query = false;
    int32_t multi_query_group_num = 1;
    int32_t n_vocab;
    int32_t n_embd;
    int32_t n_mult;
    int32_t n_head;
    int32_t n_layer;
    int32_t n_rot;
    int32_t ftype;
    int32_t n_ctx;  // this is provided as user input?
};

struct UserConfig {
    DType compt_type;
};

class Graph;

class OprModuleBase {
public:
    OprModuleBase(
            std::shared_ptr<Tensor> input, Device* device, const std::string& name);

    OprModuleBase(
            std::vector<std::shared_ptr<Tensor>> inputs, Device* device,
            const std::string& name);
    size_t get_workspace_in_byte();
    void deduce_output_shape();

    virtual void execute(
            WorkSpace* workspace, uint32_t nr_past, bool is_prefill = false);

    template <typename Op, typename... Args>
    std::vector<std::shared_ptr<Tensor>> add_opr(Args&&... args) {
        auto opr = std::make_shared<Op>(std::forward<Args>(args)...);
        m_oprs.push_back(opr);
        return opr->outputs();
    }

    std::vector<std::shared_ptr<Tensor>> get_all_weights() {
        std::vector<std::shared_ptr<Tensor>> all_weights;
        for (auto opr : m_oprs) {
            auto weights = opr->weights();
            all_weights.insert(all_weights.end(), weights.begin(), weights.end());
        }
        return all_weights;
    }

    std::vector<std::shared_ptr<Tensor>> inputs() const { return m_inputs; };
    std::shared_ptr<Tensor> input(int id = 0) const { return m_inputs[id]; };
    std::shared_ptr<Tensor> output() const { return m_output; };

    void set_input(std::shared_ptr<Tensor> input) { m_inputs.push_back(input); };
    void set_output(std::shared_ptr<Tensor> output) { m_output = output; };

    std::string name() const { return m_name; }

    Device* device() const { return m_device; }

    virtual void reset_ctx() {}

    std::vector<std::shared_ptr<OpBase>>& oprs() { return m_oprs; }

private:
    std::string m_name;
    Device* m_device;
    std::vector<std::shared_ptr<Tensor>> m_inputs;
    std::shared_ptr<Tensor> m_output;
    std::vector<std::shared_ptr<OpBase>> m_oprs;
};

template <typename Attention>
class AttentionModule : public OprModuleBase {
public:
    AttentionModule(
            Graph* graph, std::shared_ptr<Tensor> input, uint32_t embd, uint32_t head,
            uint32_t n_rot, uint32_t n_ctx, UserConfig model_config, Device* device,
            const std::string& name, int layer_id, bool fused_weights = false,
            bool bias = false, RotMode rotary_mode = RotMode::Mode0,
            bool same_bias = true)
            : OprModuleBase(input, device, name),
              m_embd(embd),
              m_head(head),
              m_rot(n_rot),
              m_graph(graph) {
        INFER_ASSERT(embd % head == 0, "Embedding and head is not match.");

        //! kqv-matmul
        m_attention_op = std::make_shared<Attention>(
                device, name, OpIOs{input}, embd, n_rot, n_ctx, head, layer_id,
                model_config.compt_type, fused_weights, bias, rotary_mode);
        oprs().push_back(m_attention_op);
        auto v_out = m_attention_op->outputs()[0];
        //! matmul proj
        bool proj_bias = same_bias ? bias : !bias;
        auto proj_out = add_opr<MatMul>(
                device, name + ".wo", OpIOs{v_out}, std::vector<size_t>{embd, embd},
                proj_bias)[0];
        set_output(proj_out);
    }

    void reset_ctx() override { m_attention_op->reset_ctx(); }

private:
    uint32_t m_embd;
    uint32_t m_head;
    uint32_t m_rot;
    Graph* m_graph;

    std::shared_ptr<Attention> m_attention_op;
};

class LlamaFFNModule : public OprModuleBase {
public:
    LlamaFFNModule(
            Graph* graph, std::shared_ptr<Tensor> input, uint32_t embd, uint32_t mult,
            UserConfig model_config, Device* device, const std::string& name);

private:
    uint32_t m_embd;
    Graph* m_graph;
};

class PredictorModule : public OprModuleBase {
public:
    PredictorModule(
            Graph* graph, std::shared_ptr<Tensor> input, uint32_t embd, uint32_t mult,
            UserConfig model_config, Device* device, const std::string& name);

private:
    uint32_t m_embd;
    Graph* m_graph;
};

class SparseFFNModule : public OprModuleBase {
public:
    SparseFFNModule(
            Graph* graph, std::shared_ptr<Tensor> input, uint32_t embd, uint32_t mult,
            UserConfig model_config, Device* device, const std::string& name);

private:
    uint32_t m_embd;
    Graph* m_graph;
};

class GlmFFNModule : public OprModuleBase {
public:
    GlmFFNModule(
            Graph* graph, std::shared_ptr<Tensor> input, uint32_t embd, uint32_t mult,
            UserConfig model_config, Device* device, const std::string& name);

private:
    uint32_t m_embd;
    Graph* m_graph;
};

class Glm2FFNModule : public OprModuleBase {
public:
    Glm2FFNModule(
            Graph* graph, std::shared_ptr<Tensor> input, uint32_t embd, uint32_t mult,
            UserConfig model_config, Device* device, const std::string& name);

private:
    uint32_t m_embd;
    Graph* m_graph;
};

class HeadModule : public OprModuleBase {
public:
    HeadModule(
            Graph* graph, std::shared_ptr<Tensor> input, uint32_t embd, uint32_t vocab,
            UserConfig model_config, Device* device, const std::string& name,
            bool bias = false, float eps = 1e-5);

    void execute(
            WorkSpace* workspace, uint32_t nr_past, bool is_prefill = false) override;

private:
    uint32_t m_embd;
    uint32_t m_vocab;
    Graph* m_graph;
};

class EmbdModule : public OprModuleBase {
public:
    EmbdModule(
            Graph* graph, std::shared_ptr<Tensor> input, uint32_t embd, uint32_t vocab,
            UserConfig model_config, Device* device, const std::string& name);

private:
    uint32_t m_embd;
    uint32_t m_vocab;
    Graph* m_graph;
};

template <class Op>
class OneOpModule : public OprModuleBase {
public:
    OneOpModule(
            Graph* graph, const std::vector<std::shared_ptr<Tensor>>& inputs,
            Device* device, const std::string& name)
            : OprModuleBase(inputs, device, name), m_graph(graph) {}

    template <typename... Args>
    std::shared_ptr<Tensor> add_opr(Args&&... args) {
        auto opr = std::make_shared<Op>(
                device(), name(), inputs(), std::forward<Args>(args)...);
        oprs().push_back(opr);
        set_output(opr->outputs()[0]);
        return opr->outputs()[0];
    }

private:
    Graph* m_graph;
};

class Graph : public std::enable_shared_from_this<Graph> {
public:
    Graph(UserConfig model_config, Device* device, const std::string& name)
            : m_name(name), m_model_config(model_config), m_device(device) {
        m_workspace = make_unique<WorkSpace>();
    }

    static std::shared_ptr<Graph> make_graph(
            UserConfig model_config, Device* device, const std::string& name);

    virtual ~Graph();

    void execute(
            std::vector<int32_t> in_token, std::vector<float>& logist, uint32_t nr_past,
            bool prefill = false);

    Device* device() { return m_device; }

    std::string name() { return m_name; }

    UserConfig model_config() { return m_model_config; }

    size_t get_workspace_in_byte();

    template <typename OpModule, typename... Args>
    std::shared_ptr<Tensor> add_module(Args&&... args) {
        auto module = std::make_shared<OpModule>(std::forward<Args>(args)...);
        m_modules.push_back(module);
        return module->output();
    }

    template <typename Op>
    std::shared_ptr<OneOpModule<Op>> add_one_opr_module(
            Graph* graph, std::vector<std::shared_ptr<Tensor>> inputs, Device* device,
            const std::string& name) {
        auto module = std::make_shared<OneOpModule<Op>>(graph, inputs, device, name);
        m_modules.push_back(module);
        return module;
    }

    void reset_ctx();

    void collect_weights();

    virtual void load_param(
            std::shared_ptr<InputFile> fin, LlmParams& param,
            std::shared_ptr<Vocab> vocab) {}

    std::string get_weight_alias(const std::string& name);

    static DType convert_dtype(int32_t type);

    bool same_input_shape(std::vector<int32_t> in_token);

    virtual void load(
            std::shared_ptr<InputFile> fin, LlmParams& param,
            std::shared_ptr<Vocab> vocab);

    virtual void construct_llm() = 0;

    virtual void set_weights_alias(){};

    virtual void post_tokenize(std::vector<Vocab::Id>& input) {}

    uint32_t get_nr_ctx() { return m_param.n_ctx; }
    uint32_t get_nr_vocab() { return m_param.n_vocab; }

    std::shared_ptr<Tensor> m_input;
    std::shared_ptr<Tensor> m_output;
    std::unordered_map<std::string, std::shared_ptr<Tensor>> m_weights_map;
    std::unordered_map<std::string, std::string> m_weights_name_aliases;
    std::vector<std::shared_ptr<OprModuleBase>> m_modules;

    LlmParams m_param;

private:
    std::string m_name;
    UserConfig m_model_config;
    Device* m_device = nullptr;

    std::shared_ptr<Tensor> m_embeddings;
    std::unique_ptr<WorkSpace> m_workspace;
};
}  // namespace inferllm
