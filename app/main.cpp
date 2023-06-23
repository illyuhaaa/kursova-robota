#include <QtWidgets>
#include <opencv2/opencv.hpp>

class ImageControlsWindow : public QWidget {
    Q_OBJECT

public:
    ImageControlsWindow(QWidget* parent = nullptr) : QWidget(parent) {
        setWindowTitle("Image Controls");

        QLabel* colorLabel = new QLabel("Color:");
        colorPicker = new QPushButton;
        connect(colorPicker, &QPushButton::clicked, this, &ImageControlsWindow::pickColor);
        fillColor = Qt::black;
        colorPicker->setStyleSheet(QString("background-color: %1").arg(fillColor.name()));

        QLabel* fillLabel = new QLabel("Fill:");
        fillTool = new QCheckBox;
        fillTool->setCheckState(Qt::Unchecked);

        QVBoxLayout* mainLayout = new QVBoxLayout;
        mainLayout->addWidget(colorLabel);
        mainLayout->addWidget(colorPicker);
        mainLayout->addWidget(fillLabel);
        mainLayout->addWidget(fillTool);
        mainLayout->addStretch();

        setLayout(mainLayout);

        QPushButton* undoButton = new QPushButton("Undo");
        connect(undoButton, &QPushButton::clicked, this, &ImageControlsWindow::undoLastAction);
        mainLayout->addWidget(undoButton);
    }

    void undoLastAction() {
        emit undoAction();
    }

signals:
    void colorPicked(const QColor& color);
    void fillToggled(bool checked);
    void undoAction();

private slots:
    void pickColor() {
        QColorDialog colorDialog(fillColor, this);
        fillColor = colorDialog.getColor();
        colorPicker->setStyleSheet(QString("background-color: %1").arg(fillColor.name()));

        emit colorPicked(fillColor);
    }

    void toggleFill(bool checked) {
        emit fillToggled(checked);
    }

private:
    QPushButton* colorPicker;
    QColor fillColor;
    QCheckBox* fillTool;
};

class ColoringPageGenerator : public QMainWindow {
    Q_OBJECT

public:
    ColoringPageGenerator(QWidget* parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("Coloring Page Generator");

        QLabel* label = new QLabel("Select an image:");
        QPushButton* button = new QPushButton("Browse");
        connect(button, &QPushButton::clicked, this, &ColoringPageGenerator::browseImage);

        QHBoxLayout* layout = new QHBoxLayout;
        layout->addWidget(label);
        layout->addWidget(button);

        QVBoxLayout* mainLayout = new QVBoxLayout;
        mainLayout->addLayout(layout);

        drawingArea = new QLabel;
        drawingArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        drawingArea->setAlignment(Qt::AlignCenter);
        drawingArea->setStyleSheet("QLabel { background-color : white; }");

        mainLayout->addWidget(drawingArea);

        QWidget* centralWidget = new QWidget(this);
        centralWidget->setLayout(mainLayout);
        setCentralWidget(centralWidget);

        drawingImage = nullptr;
        drawing = false;

        imageControlsWindow = new ImageControlsWindow;
        connect(imageControlsWindow, &ImageControlsWindow::colorPicked, this, &ColoringPageGenerator::setFillColor);
        connect(imageControlsWindow, &ImageControlsWindow::fillToggled, this, &ColoringPageGenerator::toggleFillTool);
        connect(imageControlsWindow, &ImageControlsWindow::undoAction, this, &ColoringPageGenerator::undoLastAction);

        QPushButton* saveButton = new QPushButton("Save");
        connect(saveButton, &QPushButton::clicked, this, &ColoringPageGenerator::saveImage);
        mainLayout->addWidget(saveButton);
        
    }

protected:

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && drawingImage) {
            drawing = true;
            lastPoint = event->pos() - drawingArea->pos(); 
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (drawing && drawingImage) {
            QPoint currentPoint = event->pos() - drawingArea->pos(); 
            if (drawingArea->rect().contains(currentPoint)) {
                QPainter painter(drawingImage);
                painter.setPen(fillColor);
                painter.drawLine(lastPoint, currentPoint);
                lastPoint = currentPoint;
                drawingArea->setPixmap(QPixmap::fromImage(*drawingImage));
            }
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && drawingImage) {
            drawing = false;
        }
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (drawingImage && fillToolEnabled) {
            QPoint point = event->pos() - drawingArea->pos(); 
            int x = point.x();
            int y = point.y();

            try {
                cv::Mat mask = cv::Mat::zeros(coloringPage.rows + 2, coloringPage.cols + 2, CV_8U);
                cv::floodFill(coloringPage, mask, cv::Point(x, y), cv::Scalar(fillColor.red(), fillColor.green(), fillColor.blue()));


                drawingImage = new QImage(coloringPage.data, coloringPage.cols, coloringPage.rows, coloringPage.step, QImage::Format_RGB888);
                drawingArea->setPixmap(QPixmap::fromImage(*drawingImage));
                addToColoringPageHistory(coloringPage.clone());
            }
            catch (cv::Exception& e) {
                QMessageBox::critical(this, "Error", QString("Failed to perform flood fill: %1").arg(e.what()));
            }
        }
    }

private slots:
    void browseImage() {
        QString imagePath = QFileDialog::getOpenFileName(this, "Select Image", "", "Images (*.png *.jpg *.jpeg)");
        if (!imagePath.isEmpty()) {
            generateColoringPage(imagePath);
        }
    }
    void postProcessContours(std::vector<std::vector<cv::Point>>& contours, double minContourArea, int smoothingIterations, int thickness)
    {
        contours.erase(std::remove_if(contours.begin(), contours.end(), [minContourArea](const std::vector<cv::Point>& contour) {
            return cv::contourArea(contour) < minContourArea;
            }), contours.end());

        for (std::vector<cv::Point>& contour : contours)
        {
           
            cv::approxPolyDP(contour, contour, smoothingIterations, true);

            cv::drawContours(coloringPage, std::vector<std::vector<cv::Point>>{contour}, 0, cv::Scalar(0, 0, 0), thickness, cv::LINE_AA);


            cv::Mat kernelClosing = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
            cv::morphologyEx(coloringPage, coloringPage, cv::MORPH_CLOSE, kernelClosing);

            
        }
    }

    void generateColoringPage(const QString& imagePath) {
        cv::Mat image = cv::imread(imagePath.toStdString());

        if (image.empty()) {
            QMessageBox::critical(this, "Error", "Failed to load the image.");
            return;
        }

        try {
            
            int maxWidth = drawingArea->width();
            int maxHeight = drawingArea->height();
            double aspectRatio = static_cast<double>(image.cols) / image.rows;
            int newWidth = maxWidth;
            int newHeight = static_cast<int>(newWidth / aspectRatio);

            if (newHeight > maxHeight) {
                newHeight = maxHeight;
                newWidth = static_cast<int>(newHeight * aspectRatio);
            }

            cv::resize(image, image, cv::Size(newWidth, newHeight));

            drawingArea->setFixedSize(newWidth, newHeight);

            cv::resize(image, image, cv::Size(drawingArea->width(), drawingArea->height()));



            cv::Mat grayscale;
            cv::cvtColor(image, grayscale, cv::COLOR_BGR2GRAY);

            cv::Mat binaryImage;
            cv::adaptiveThreshold(grayscale, binaryImage, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY_INV, 15, 10);

            cv::Mat dilatedImage;
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(30, 30));
            cv::dilate(binaryImage, dilatedImage, kernel);

            cv::Mat erodedImage;
            cv::erode(dilatedImage, erodedImage, kernel);

            coloringPage = erodedImage.clone();

            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(coloringPage, contours, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);

            postProcessContours(contours, 150.0, 30, 1000);

            cv::Mat contourMask(coloringPage.size(), CV_8U, cv::Scalar(0));
            for (const auto& contour : contours) {
                cv::drawContours(contourMask, std::vector<std::vector<cv::Point>>{contour}, 0, cv::Scalar(255), cv::FILLED);
            }

            cv::Mat kernelClosing = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
            cv::morphologyEx(contourMask, contourMask, cv::MORPH_CLOSE, kernelClosing);
            cv::bitwise_and(coloringPage, contourMask, coloringPage);

            coloringPage = cv::Mat(image.size(), CV_8UC3, cv::Scalar(255, 255, 255));
            coloringPage.setTo(cv::Scalar(0, 0, 0), binaryImage);

            double gapAreaThreshold = 50.0;
            for (const auto& contour : contours) {
                double contourArea = cv::contourArea(contour);
                if (contourArea < gapAreaThreshold) {
                    cv::Mat gapMask(coloringPage.size(), CV_8U, cv::Scalar(0));
                    cv::drawContours(gapMask, std::vector<std::vector<cv::Point>>{contour}, 0, cv::Scalar(255), cv::FILLED);

                    cv::inpaint(coloringPage, gapMask, coloringPage, 3, cv::INPAINT_TELEA);
                }
            }

            addToColoringPageHistory(coloringPage.clone());

            drawingImage = new QImage(coloringPage.data, coloringPage.cols, coloringPage.rows, coloringPage.step, QImage::Format_RGB888);
            drawingArea->setFixedSize(image.cols, image.rows);

            setMinimumSize(image.cols, image.rows);
        }
        catch (cv::Exception& e) {
            QMessageBox::critical(this, "Error", QString("Failed to generate coloring page: %1").arg(e.what()));
        }
    }

    void saveImage() {
        QString savePath = QFileDialog::getSaveFileName(this, "Save Image", "", "Images (*.png *.jpg *.jpeg)");
        if (!savePath.isEmpty()) {
            if (drawingImage && drawingImage->save(savePath)) {
                QMessageBox::information(this, "Success", "Image saved successfully.");
            }
            else {
                QMessageBox::critical(this, "Error", "Failed to save the image.");
            }
        }
    }
   

public:
    void setFillColor(const QColor& color) {
        fillColor = color;
    }

    void toggleFillTool(bool checked) {
        fillToolEnabled = checked;
    }

    void undoLastAction() {
        if (coloringPageHistory.size() > 1) {
            coloringPageHistory.pop_back();
            coloringPage = coloringPageHistory.back().clone();
            drawingImage = new QImage(coloringPage.data, coloringPage.cols, coloringPage.rows, coloringPage.step, QImage::Format_RGB888);
            drawingArea->setPixmap(QPixmap::fromImage(*drawingImage));
        }
    }

private:
    QLabel* drawingArea;
    QImage* drawingImage;
    bool drawing;
    QPoint lastPoint;
    cv::Mat coloringPage;
    std::vector<cv::Mat> coloringPageHistory;
    ImageControlsWindow* imageControlsWindow;
    QColor fillColor;
    bool fillToolEnabled;

    void addToColoringPageHistory(const cv::Mat& page) {
        coloringPageHistory.push_back(page.clone());
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    ColoringPageGenerator generator;
    generator.showMaximized();

    ImageControlsWindow imageControlsWindow;
    imageControlsWindow.show();

    QObject::connect(&imageControlsWindow, &ImageControlsWindow::colorPicked, &generator, &ColoringPageGenerator::setFillColor);
    QObject::connect(&imageControlsWindow, &ImageControlsWindow::fillToggled, &generator, &ColoringPageGenerator::toggleFillTool);
    QObject::connect(&imageControlsWindow, &ImageControlsWindow::undoAction, &generator, &ColoringPageGenerator::undoLastAction);

    return app.exec();
}

#include "main.moc"

