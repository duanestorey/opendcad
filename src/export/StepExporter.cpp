#include "StepExporter.h"
#include "Debug.h"
#include <STEPControl_Writer.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_PrinterOStream.hxx>

namespace opendcad {

bool StepExporter::write(const TopoDS_Shape& shape, const std::string& path) {
    DEBUG_INFO("Writing STEP file [" << path << "]");

    // Silence OCCT messaging
    Message_Gravity aGravity = Message_Alarm;
    for (Message_SequenceOfPrinters::Iterator aPrinterIter(Message::DefaultMessenger()->Printers());
         aPrinterIter.More(); aPrinterIter.Next()) {
        aPrinterIter.Value()->SetTraceLevel(aGravity);
    }

    STEPControl_Writer writer;
    if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) return false;
    bool result = writer.Write(path.c_str()) == IFSelect_RetDone;
    DEBUG_INFO("...STEP file written");
    return result;
}

} // namespace opendcad
