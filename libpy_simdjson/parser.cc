#include <algorithm>
#include <memory>
#include <string>

#include <libpy/autoclass.h>
#include <libpy/autofunction.h>
#include <libpy/automodule.h>
#include <libpy/build_tuple.h>
#include <libpy/to_object.h>

#include "simdjson.h"

namespace py::dispatch {

template<>
struct to_object<simdjson::dom::array> {
    static py::owned_ref<> f(const simdjson::dom::array& v) {
        return sequence_to_list(v);
    }
};

template<>
struct to_object<simdjson::dom::object> : public map_to_object<simdjson::dom::object> {};

template<>
struct to_object<simdjson::dom::element> {
    static py::owned_ref<> f(const simdjson::dom::element& element) {
        auto element_type = element.type();
        switch (element_type) {
        case simdjson::dom::element_type::ARRAY:
            return py::to_object(simdjson::dom::array(element));
        case simdjson::dom::element_type::OBJECT:
            return py::to_object(simdjson::dom::object(element));
        case simdjson::dom::element_type::INT64:
            return py::to_object(int64_t(element));
        case simdjson::dom::element_type::UINT64:
            return py::to_object(uint64_t(element));
        case simdjson::dom::element_type::DOUBLE:
            return py::to_object(double(element));
        case simdjson::dom::element_type::STRING:
            return py::to_object(std::string_view(element));
        case simdjson::dom::element_type::BOOL:
            return py::to_object(bool(element));
        case simdjson::dom::element_type::NULL_VALUE:
            return py::none;
        default:
            throw py::exception(PyExc_ValueError, "Unexpected element_type encountered");
        }
    }
};

template<typename T>
struct to_object<simdjson::simdjson_result<T>> {
    static py::owned_ref<> f(const simdjson::simdjson_result<T>& maybe_result) {
        simdjson::dom::element result;
        auto error = maybe_result.get(result);
        if (error) {
            // throw
            throw py::exception(PyExc_ValueError, "Issue with that parse");
        }
        return py::to_object(result);
    }
};

template<>
struct to_object<simdjson::dom::key_value_pair> {
    static py::owned_ref<> f(const simdjson::dom::key_value_pair& key_value_pair) {
        return py::build_tuple(key_value_pair.key, key_value_pair.value);
    }
};

}  // namespace py::dispatch

namespace libpy_simdjson {

class parser : public std::enable_shared_from_this<parser> {
private:
    simdjson::dom::parser m_parser;

public:
    parser() = default;

    std::shared_ptr<parser> getptr() {
        return shared_from_this();
    }

    py::owned_ref<> load(const std::string& filename);

    py::owned_ref<> loads(const std::string& in_string);
};

class object_element {
private:
    std::shared_ptr<parser> m_parser;
    simdjson::dom::object m_value;

public:
    object_element(std::shared_ptr<parser> parser_pntr, simdjson::dom::object value)
        : m_parser(parser_pntr), m_value(value) {}

    py::owned_ref<> operator[](const std::string& field);

    py::owned_ref<> at(const std::string& json_pntr);

    std::vector<py::owned_ref<>> items() {
        std::vector<py::owned_ref<>> out;
        for (auto [key, value] : m_value) {
            out.push_back(py::build_tuple(key, value));
        }
        return out;
    }

    std::vector<std::string_view> keys() {
        std::vector<std::string_view> out;
        for (auto [key, value] : m_value) {
            out.push_back(key);
        }
        return out;
    }

    py::owned_ref<> values();

    py::owned_ref<> as_dict() {
        return py::to_object(m_value);
    }

    std::size_t size() {
        return m_value.size();
    }

    typedef simdjson::dom::object::iterator iterator;

    iterator begin() {
        return m_value.begin();
    }

    iterator end() {
        return m_value.end();
    }

};

class array_element {
private:
    std::shared_ptr<parser> m_parser;
    simdjson::dom::array m_value;

public:
    array_element(std::shared_ptr<parser> parser_pntr, simdjson::dom::array value)
        : m_parser(parser_pntr), m_value(value) {}

    py::owned_ref<> at(const std::string& json_pntr);

    // py::owned_ref<> count(py::borrowed_ref elem) {
    //     return std::count(m_value.begin(), m_value.end(), elem);
    // }
    py::owned_ref<> operator[](const std::size_t& index);

    py::owned_ref<> as_list() {
        return py::to_object(m_value);
    }

    std::size_t size() {
        return m_value.size();
    }

    typedef simdjson::dom::array::iterator iterator;

    iterator begin() {
        return m_value.begin();
    }

    iterator end() {
        return m_value.end();
    }
};

py::owned_ref<> disambiguate_result(std::shared_ptr<parser> parser_pntr,
                                    simdjson::dom::element result) {
    auto result_type = result.type();
    switch (result_type) {
    case simdjson::dom::element_type::ARRAY:
        return py::autoclass<array_element>::construct(parser_pntr, result);
    case simdjson::dom::element_type::OBJECT:
        return py::autoclass<object_element>::construct(parser_pntr, result);
    default:
        return py::to_object(result);
    }
}

py::owned_ref<> parser::load(const std::string& filename) {
    if (weak_from_this().use_count() > 1) {
        throw py::exception(PyExc_ValueError,
                            "cannot reparse while live objects exist from a prior parse");
    }
    auto maybe_result = m_parser.load(filename);
    simdjson::dom::element result;
    auto error = maybe_result.get(result);
    if (error) {
        // throw
        throw py::exception(PyExc_ValueError, "Issue with that parse");
    }
    return disambiguate_result(shared_from_this(), result);
}

py::owned_ref<> parser::loads(const std::string& in_string) {
    if (weak_from_this().use_count() > 1) {
        throw py::exception(PyExc_ValueError,
                            "cannot reparse while live objects exist from a prior parse");
    }
    auto maybe_result = m_parser.parse(in_string);
    simdjson::dom::element result;
    auto error = maybe_result.get(result);
    if (error) {
        // throw
        throw py::exception(PyExc_ValueError, "Issue with that parse");
    }
    return disambiguate_result(shared_from_this(), result);
}

py::owned_ref<> object_element::operator[](const std::string& field) {
    return disambiguate_result(m_parser, m_value[field]);
}

py::owned_ref<> object_element::at(const std::string& json_pntr) {
    simdjson::dom::element result;
    auto maybe_result = m_value.at(json_pntr);
    auto error = maybe_result.get(result);
    if (error) {
        throw py::exception(PyExc_ValueError, "Could not get element at ", json_pntr);
    }
    return disambiguate_result(m_parser, result);
}

py::owned_ref<> array_element::at(const std::string& json_pntr) {
    simdjson::dom::element result;
    auto maybe_result = m_value.at(json_pntr);
    auto error = maybe_result.get(result);
    if (error) {
        throw py::exception(PyExc_ValueError, "Could not get element at ", json_pntr);
    }
    return disambiguate_result(m_parser, result);
}

py::owned_ref<> array_element::operator[](const std::size_t& index) {
    simdjson::dom::element result;
    auto maybe_result = m_value.at(index);
    auto error = maybe_result.get(result);
    if (error) {
        throw py::exception(PyExc_ValueError, "Could not get element at ", index);
    }
    return disambiguate_result(m_parser, result);
}

py::owned_ref<> load(const std::string& filename) {
    return std::make_shared<parser>()->load(filename);
}

py::owned_ref<> loads(const std::string& in_string) {
    return std::make_shared<parser>()->loads(in_string);
}

using namespace std::string_literals;

LIBPY_AUTOMODULE(libpy_simdjson,
                 simdjson,
                 ({py::autofunction<load>("load"), py::autofunction<loads>("loads")}))
(py::borrowed_ref<> m) {
    py::owned_ref a = py::autoclass<parser>(PyModule_GetName(m.get()) + ".Parser"s)
                          .new_<>()
                          .doc("Base parser")  // add a class docstring
                          .def<&parser::load>("load")
                          .def<&parser::loads>("loads")
                          .type();
    PyObject_SetAttrString(m.get(), "Parser", static_cast<PyObject*>(a));
    py::owned_ref b = py::autoclass<object_element>(PyModule_GetName(m.get()) +
                                                    ".Object"s)
                          .mapping<std::string>()
                          .def<&object_element::at>("at")
                          .def<&object_element::as_dict>("as_dict")
                          .def<&object_element::items>("items")
                          .def<&object_element::keys>("keys")
                          .len()
                          .iter()
                          .type();
    PyObject_SetAttrString(m.get(), "Object", static_cast<PyObject*>(b));

    py::owned_ref c = py::autoclass<array_element>(PyModule_GetName(m.get()) + ".Array"s)
                          .def<&array_element::at>("at")
                          .def<&array_element::as_list>("as_list")
                          .mapping<std::size_t>()
                          .len()
                          .iter()
                          .type();
    PyObject_SetAttrString(m.get(), "Array", static_cast<PyObject*>(c));

    return false;
}
}  // namespace libpy_simdjson
