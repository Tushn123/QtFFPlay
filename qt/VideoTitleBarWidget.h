#ifndef VIDEOTITLEBARWIDGET_H
#define VIDEOTITLEBARWIDGET_H

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include "VideoGLWidget.h"

/**
 * 可编辑的路径标签
 * 双击进入编辑模式，回车确认，Esc取消
 */
class EditablePathLabel : public QLineEdit
{
    Q_OBJECT
public:
    explicit EditablePathLabel(QWidget *parent = nullptr);
    
    void setPath(const QString &path);
    QString path() const { return m_currentPath; }

signals:
    void pathChanged(const QString &newPath);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onEditingFinished();

private:
    void enterEditMode();
    void exitEditMode(bool confirm);
    void updateDisplayText();
    
    QString m_currentPath;      // 当前实际路径
    bool m_editing;             // 是否处于编辑模式
};

class VideoTitleBarWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VideoTitleBarWidget(QWidget *parent = nullptr);

    /**
     * 设置当前缩放模式
     * @param mode 缩放模式
     */
    void setScaleMode(ScaleMode mode);
    
    /**
     * 获取当前缩放模式
     */
    ScaleMode scaleMode() const;
    
    /**
     * 设置当前视频路径（显示在标题栏）
     */
    void setVideoPath(const QString &path);
    
    /**
     * 获取当前视频路径
     */
    QString videoPath() const;

signals:
    /**
     * 缩放模式改变信号
     */
    void scaleModeChanged(ScaleMode mode);
    
    /**
     * 打开文件信号
     * @param filePath 用户选择的文件路径
     */
    void openFileRequested(const QString &filePath);

private slots:
    void onScaleModeComboChanged(int index);
    void onOpenButtonClicked();
    void onPathEdited(const QString &newPath);

public:
    QPushButton *openButton;
    QComboBox *scaleModeCombo;

private:
    void initUI();
    void initConnect();

    EditablePathLabel *titleEdit;
};

#endif // VIDEOTITLEBARWIDGET_H
