#include "StepExporter.h"
#include "Shape.h"
#include "Color.h"
#include "Debug.h"
#include <STEPControl_Writer.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFApp_Application.hxx>
#include <TDocStd_Document.hxx>
#include <TDataStd_Name.hxx>
#include <Quantity_Color.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_PrinterOStream.hxx>

namespace opendcad {

static void silenceOcct() {
    Message_Gravity aGravity = Message_Alarm;
    for (Message_SequenceOfPrinters::Iterator aPrinterIter(Message::DefaultMessenger()->Printers());
         aPrinterIter.More(); aPrinterIter.Next()) {
        aPrinterIter.Value()->SetTraceLevel(aGravity);
    }
}

bool StepExporter::write(const TopoDS_Shape& shape, const std::string& path) {
    DEBUG_INFO("Writing STEP file [" << path << "]");
    silenceOcct();

    STEPControl_Writer writer;
    if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) return false;
    bool result = writer.Write(path.c_str()) == IFSelect_RetDone;
    DEBUG_INFO("...STEP file written");
    return result;
}

bool StepExporter::writeWithMetadata(const std::vector<ShapePtr>& shapes,
                                      const std::string& name,
                                      const std::string& path) {
    DEBUG_INFO("Writing STEP file with metadata [" << path << "] (" << shapes.size() << " shapes)");
    silenceOcct();

    // Check if any shape has color metadata — if not, use simple writer
    bool hasColors = false;
    for (const auto& s : shapes) {
        if (s->color()) { hasColors = true; break; }
    }

    if (!hasColors && shapes.size() == 1) {
        return write(shapes[0]->getShape(), path);
    }

    // Create XDE document
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    Handle(TDocStd_Document) doc;
    app->NewDocument("MDTV-XCAF", doc);

    Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    Handle(XCAFDoc_ColorTool) colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

    for (size_t i = 0; i < shapes.size(); ++i) {
        TDF_Label label = shapeTool->AddShape(shapes[i]->getShape());

        // Set name
        std::string shapeName = name + "_" + std::to_string(i);
        TDataStd_Name::Set(label, TCollection_ExtendedString(shapeName.c_str(), true));

        // Set color if present
        if (shapes[i]->color()) {
            const auto& c = *shapes[i]->color();
            Quantity_Color qc(c.r, c.g, c.b, Quantity_TOC_RGB);
            colorTool->SetColor(label, qc, XCAFDoc_ColorSurf);
        }
    }

    // Write
    STEPCAFControl_Writer writer;
    writer.SetColorMode(true);
    writer.SetNameMode(true);
    if (!writer.Transfer(doc, STEPControl_AsIs)) return false;
    bool result = writer.Write(path.c_str()) == IFSelect_RetDone;

    app->Close(doc);
    DEBUG_INFO("...STEP file with metadata written");
    return result;
}

} // namespace opendcad
