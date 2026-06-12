// psdparse Python bindings (minimal)
//
// Exposes PSDFile / LayerInfo / Header just enough to load a PSD, enumerate
// layers, and pull raw BGRA pixels into Python bytes objects so tests can
// hash them or hand them to PIL / numpy.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "psdfile.h"
#include "psdparse.h"

#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace py = pybind11;

namespace {

// std::u16string (PSD's UTF-16BE on-disk -> host-order UTF-16 in memory)
// to Python str. UTF-16 code units transfer cleanly via py::str(u16string).
py::object u16ToStr(const psd::u16str &s) {
  return py::cast(s);
}

// Wrap getMergedImage / getLayerImage results in a py::bytes (BGRA, 4 bytes/px).
py::bytes mergedImage(psd::PSDFile &self) {
  if (!self.isLoaded) throw std::runtime_error("PSD not loaded");
  if (!self.imageData) throw std::runtime_error("no merged image stored in this PSD");
  size_t n = (size_t)self.header.width * (size_t)self.header.height * 4;
  std::string buf(n, '\0');
  self.getMergedImage(buf.data(), psd::BGRA_LE, 0);
  return py::bytes(buf);
}

py::bytes layerImage(psd::PSDFile &self, int index, const std::string &mode) {
  if (!self.isLoaded) throw std::runtime_error("PSD not loaded");
  if (index < 0 || index >= (int)self.layerList.size())
    throw std::out_of_range("layer index out of range");
  psd::ImageMode m;
  if      (mode == "image")  m = psd::IMAGE_MODE_IMAGE;
  else if (mode == "mask")   m = psd::IMAGE_MODE_MASK;
  else if (mode == "masked") m = psd::IMAGE_MODE_MASKEDIMAGE;
  else throw std::invalid_argument("mode must be 'image', 'mask' or 'masked'");
  psd::LayerInfo &lay = self.layerList[(size_t)index];
  if (lay.width <= 0 || lay.height <= 0) return py::bytes();
  size_t n = (size_t)lay.width * (size_t)lay.height * 4;
  std::string buf(n, '\0');
  self.getLayerImage(lay, buf.data(), psd::BGRA_LE, lay.width * 4, m);
  return py::bytes(buf);
}

} // namespace

PYBIND11_MODULE(psdparse, m) {
  m.doc() = "psdparse: PSD reader (Boost-free, no kirikiri deps).";

  py::enum_<psd::LayerType>(m, "LayerType")
    .value("NORMAL", psd::LAYER_TYPE_NORMAL)
    .value("HIDDEN", psd::LAYER_TYPE_HIDDEN)
    .value("FOLDER", psd::LAYER_TYPE_FOLDER)
    .value("ADJUST", psd::LAYER_TYPE_ADJUST)
    .value("FILL",   psd::LAYER_TYPE_FILL)
    .value("TEXT",   psd::LAYER_TYPE_TEXT)
    .export_values();

  py::enum_<psd::BlendMode>(m, "BlendMode")
    .value("INVALID",      psd::BLEND_MODE_INVALID)
    .value("NORMAL",       psd::BLEND_MODE_NORMAL)
    .value("DISSOLVE",     psd::BLEND_MODE_DISSOLVE)
    .value("DARKEN",       psd::BLEND_MODE_DARKEN)
    .value("MULTIPLY",     psd::BLEND_MODE_MULTIPLY)
    .value("COLOR_BURN",   psd::BLEND_MODE_COLOR_BURN)
    .value("LINEAR_BURN",  psd::BLEND_MODE_LINEAR_BURN)
    .value("LIGHTEN",      psd::BLEND_MODE_LIGHTEN)
    .value("SCREEN",       psd::BLEND_MODE_SCREEN)
    .value("COLOR_DODGE",  psd::BLEND_MODE_COLOR_DODGE)
    .value("LINEAR_DODGE", psd::BLEND_MODE_LINEAR_DODGE)
    .value("OVERLAY",      psd::BLEND_MODE_OVERLAY)
    .value("SOFT_LIGHT",   psd::BLEND_MODE_SOFT_LIGHT)
    .value("HARD_LIGHT",   psd::BLEND_MODE_HARD_LIGHT)
    .value("VIVID_LIGHT",  psd::BLEND_MODE_VIVID_LIGHT)
    .value("LINEAR_LIGHT", psd::BLEND_MODE_LINEAR_LIGHT)
    .value("PIN_LIGHT",    psd::BLEND_MODE_PIN_LIGHT)
    .value("HARD_MIX",     psd::BLEND_MODE_HARD_MIX)
    .value("DIFFERENCE",   psd::BLEND_MODE_DIFFERENCE)
    .value("EXCLUSION",    psd::BLEND_MODE_EXCLUSION)
    .value("HUE",          psd::BLEND_MODE_HUE)
    .value("SATURATION",   psd::BLEND_MODE_SATURATION)
    .value("COLOR",        psd::BLEND_MODE_COLOR)
    .value("LUMINOSITY",   psd::BLEND_MODE_LUMINOSITY)
    .value("PASS_THROUGH", psd::BLEND_MODE_PASS_THROUGH)
    .value("DARKER_COLOR", psd::BLEND_MODE_DARKER_COLOR)
    .value("LIGHTER_COLOR",psd::BLEND_MODE_LIGHTER_COLOR)
    .value("SUBTRACT",     psd::BLEND_MODE_SUBTRACT)
    .value("DIVIDE",       psd::BLEND_MODE_DIVIDE);

  py::class_<psd::Header>(m, "Header")
    .def_readonly("version",  &psd::Header::version)
    .def_readonly("channels", &psd::Header::channels)
    .def_readonly("height",   &psd::Header::height)
    .def_readonly("width",    &psd::Header::width)
    .def_readonly("depth",    &psd::Header::depth)
    .def_readonly("mode",     &psd::Header::mode);

  py::class_<psd::ChannelInfo>(m, "ChannelInfo")
    .def_readonly("id",     &psd::ChannelInfo::id)
    .def_readonly("length", &psd::ChannelInfo::length)
    .def("is_mask", &psd::ChannelInfo::isMaskChannel);

  py::class_<psd::LayerInfo>(m, "LayerInfo")
    .def_readonly("top",     &psd::LayerInfo::top)
    .def_readonly("left",    &psd::LayerInfo::left)
    .def_readonly("bottom",  &psd::LayerInfo::bottom)
    .def_readonly("right",   &psd::LayerInfo::right)
    .def_readonly("width",   &psd::LayerInfo::width)
    .def_readonly("height",  &psd::LayerInfo::height)
    .def_readonly("opacity", &psd::LayerInfo::opacity)
    .def_readonly("fill_opacity",  &psd::LayerInfo::fill_opacity)
    .def_readonly("clipping",      &psd::LayerInfo::clipping)
    .def_readonly("blend_mode_key",&psd::LayerInfo::blendModeKey)
    .def_readonly("blend_mode",    &psd::LayerInfo::blendMode)
    .def_readonly("layer_type",    &psd::LayerInfo::layerType)
    .def_readonly("layer_id",      &psd::LayerInfo::layerId)
    .def_readonly("channels",      &psd::LayerInfo::channels)
    .def_property_readonly("name", [](const psd::LayerInfo &l) {
        return l.extraData.layerName;  // std::string (raw bytes, original encoding)
    })
    .def_property_readonly("name_unicode", [](const psd::LayerInfo &l) {
        return u16ToStr(l.layerNameUnicode);
    })
    .def_property_readonly("visible",                [](const psd::LayerInfo &l){ return l.isVisible(); })
    .def_property_readonly("transparency_protected", [](const psd::LayerInfo &l){ return l.isTransparencyProtected(); })
    .def_property_readonly("obsolete",               [](const psd::LayerInfo &l){ return l.isObsolete(); })
    .def_property_readonly("pixel_data_irrelevant",  [](const psd::LayerInfo &l){ return l.isPixelDataIrrelevant(); });

  py::class_<psd::PSDFile>(m, "PSDFile")
    .def(py::init<>())
    .def("load",
         [](psd::PSDFile &self, const std::string &path) {
            return self.load(path.c_str());
         },
         py::arg("path"),
         "Memory-map the file at `path` (UTF-8) and parse. The file stays "
         "open and layer pixels are paged in lazily.")
    .def("load_bytes",
         [](psd::PSDFile &self, py::bytes b) {
            py::buffer_info info(py::buffer(b).request());
            return self.loadFromMemory((const uint8_t *)info.ptr, (size_t)info.size);
         },
         py::arg("data"),
         "Parse a PSD already loaded into a Python bytes object. The bytes "
         "are copied into an internal vector.")
    .def("load_streamed",
         [](psd::PSDFile &self, const std::string &path) {
            // std::ifstream + StreamReader 経由 (mmap を使わずシークと read のみで処理)。
            // Win32 では ifstream を unicode path で開くため UTF-8 → wide。
#ifdef _WIN32
            std::wstring wpath = psd::utf8ToWide(path.c_str());
            if (wpath.empty()) return false;
            auto s = std::make_unique<std::ifstream>(wpath, std::ios::binary);
#else
            auto s = std::make_unique<std::ifstream>(path, std::ios::binary);
#endif
            if (!s || !*s) return false;
            return self.loadFromStream(std::move(s));
         },
         py::arg("path"),
         "Open `path` (UTF-8) as a std::ifstream and parse via StreamReader. "
         "Demonstrates that the parser also accepts arbitrary seekable streams "
         "(this is the code path the kirikiri plugin will use on top of "
         "iTJSBinaryStream).")
    .def("save",
         [](psd::PSDFile &self, const std::string &path) {
            return self.save(path.c_str());
         },
         py::arg("path"),
         "Save the currently loaded data as a PSD file at `path` (UTF-8). "
         "Round-trip-fidelity is the target: load(p) -> save(q) yields a PSD "
         "with structurally identical layers/header/image data.")
    .def_readonly("is_loaded", &psd::PSDFile::isLoaded)
    .def_readonly("header",    &psd::PSDFile::header)
    .def_readonly("layers",    &psd::PSDFile::layerList)
    .def_readonly("merged_alpha", &psd::PSDFile::mergedAlpha)
    .def("merged_image", &mergedImage)
    .def("layer_image", &layerImage,
         py::arg("index"), py::arg("mode") = "masked",
         "Extract pixels for layer `index` as BGRA bytes. "
         "mode: 'masked' (default), 'image' (no mask), 'mask' (mask only).");

  // Enum-like ints exposed as module attributes for convenience
  m.attr("LAYER_TYPE_NORMAL") = (int)psd::LAYER_TYPE_NORMAL;
  m.attr("LAYER_TYPE_HIDDEN") = (int)psd::LAYER_TYPE_HIDDEN;
  m.attr("LAYER_TYPE_FOLDER") = (int)psd::LAYER_TYPE_FOLDER;
  m.attr("LAYER_TYPE_ADJUST") = (int)psd::LAYER_TYPE_ADJUST;
  m.attr("LAYER_TYPE_FILL")   = (int)psd::LAYER_TYPE_FILL;

  m.attr("COLOR_MODE_BITMAP")       = (int)psd::COLOR_MODE_BITMAP;
  m.attr("COLOR_MODE_GRAYSCALE")    = (int)psd::COLOR_MODE_GRAYSCALE;
  m.attr("COLOR_MODE_INDEXED")      = (int)psd::COLOR_MODE_INDEXED;
  m.attr("COLOR_MODE_RGB")          = (int)psd::COLOR_MODE_RGB;
  m.attr("COLOR_MODE_CMYK")         = (int)psd::COLOR_MODE_CMYK;
  m.attr("COLOR_MODE_MULTICHANNEL") = (int)psd::COLOR_MODE_MULTICHANNEL;
  m.attr("COLOR_MODE_DUOTONE")      = (int)psd::COLOR_MODE_DUOTONE;
  m.attr("COLOR_MODE_LAB")          = (int)psd::COLOR_MODE_LAB;
}
