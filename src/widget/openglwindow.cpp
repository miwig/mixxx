#include "widget/openglwindow.h"

#include <QApplication>
#include <QResizeEvent>

#include "moc_openglwindow.cpp"
#include "widget/tooltipqopengl.h"
#include "widget/trackdroptarget.h"
#include "widget/wglwidget.h"

OpenGLWindow::OpenGLWindow(WGLWidget* pWidget)
        : m_pWidget(pWidget),
          m_dirty(false) {
    QSurfaceFormat format;
    format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);

    // setSwapInterval sets the application preferred swap interval
    // in minimum number of video frames that are displayed before a buffer swap occurs
    // - 0 will turn the vertical refresh syncing off
    // - 1 (default) means swapping after drawig a video frame to the buffer
    // - n means swapping after drawing n video frames to the buffer
    //
    // The vertical sync setting requested by the OpenGL application, can be overwritten
    // if a user changes the "Wait for vertical refresh" setting in AMD graphic drivers
    // for Windows.

#if defined(__APPLE__)
    // On OS X, syncing to vsync has good performance FPS-wise and
    // eliminates tearing. (This is an comment from pre QOpenGLWindow times)
    format.setSwapInterval(1);
#else
    // It seems that on Windows (at least for some AMD drivers), the setting 1 is not
    // not properly handled. We saw frame rates divided by exact integers, like it should
    // be with values >1 (see https://github.com/mixxxdj/mixxx/issues/11617)
    // Reported as https://bugreports.qt.io/browse/QTBUG-114882
    // On Linux, horrible FPS were seen with "VSync off" before switching to QOpenGLWindow too
    format.setSwapInterval(0);
#endif
    setFormat(format);
}

OpenGLWindow::~OpenGLWindow() {
}

void OpenGLWindow::initializeGL() {
    if (m_pWidget) {
        m_pWidget->initializeGL();
    }
}

void OpenGLWindow::paintGL() {
    if (m_pWidget && isExposed()) {
        if (m_dirty) {
            // Extra render and swap to avoid flickering when resizing
            m_pWidget->paintGL();
            m_pWidget->swapBuffers();
            m_dirty = false;
        }
        m_pWidget->paintGL();
    }
}

void OpenGLWindow::resizeGL(int w, int h) {
    if (m_pWidget) {
        // QGLWidget::resizeGL has a valid context (QOpenGLWindow::resizeGL does not), so we
        // mimic the same behaviour
        m_pWidget->makeCurrentIfNeeded();
        // QGLWidget::resizeGL has devicePixelRatio applied, so we mimic the same behaviour
        m_pWidget->resizeGL(static_cast<int>(static_cast<float>(w) * devicePixelRatio()),
                static_cast<int>(static_cast<float>(h) * devicePixelRatio()));
        m_dirty = true;
        m_pWidget->doneCurrent();
    }
}

void OpenGLWindow::widgetDestroyed() {
    m_pWidget = nullptr;
}

bool OpenGLWindow::event(QEvent* pEv) {
    // From here we call QApplication::sendEvent(m_pWidget, ev) to trigger
    // handling of the event as if it were received by the main window.
    // With drag move and drag leave events it may happen that this function
    // gets called recursive, potentially resulting in infinite recursion
    // and a stack overflow. The boolean m_handlingEvent protects against
    // this recursion.
    const auto t = pEv->type();

    bool result = QOpenGLWindow::event(pEv);

    if (m_pWidget) {
        // Tooltip don't work by forwarding the events. This mimics the
        // tooltip behavior.
        if (t == QEvent::MouseMove) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            QPoint eventPosition = dynamic_cast<QMouseEvent*>(pEv)->globalPosition().toPoint();
#else
            QPoint eventPosition = dynamic_cast<QMouseEvent*>(pEv)->globalPos();
#endif
            ToolTipQOpenGL::singleton().start(m_pWidget, eventPosition);
        }
        if (t == QEvent::Leave) {
            ToolTipQOpenGL::singleton().stop();
        }

        if (t == QEvent::DragEnter || t == QEvent::DragMove ||
                t == QEvent::DragLeave || t == QEvent::Drop) {
            // Drag & Drop events are not delivered correctly when using QApplication::sendEvent
            // and even result in a recursive call to this method, so we use our own mechanism.
            if (m_pWidget->trackDropTarget()) {
                return m_pWidget->trackDropTarget()->handleDragAndDropEventFromWindow(pEv);
            }

            pEv->ignore();
            return false;
        }

        if (t == QEvent::Resize || t == QEvent::Move || t == QEvent::Expose) {
            // - Resize and Move
            //    Any change to the geometry comes from m_pWidget and its child m_pContainerWidget.
            //    If we send the resulting events back to the m_pWidget we will quickly overflow
            //    the event queue with repeated resize and move events.
            // - Expose
            //    This event is only for windows
            return result;
        }

        // Send all remaining events to the widget that owns the window
        // container widget that contains this QOpenGLWindow. With this mouse
        // events, keyboard events, etc all arrive as intended, including the
        // events for the WWaveformViewer that contains the waveform widget.
        QApplication::sendEvent(m_pWidget, pEv);
    }

    return result;
}
