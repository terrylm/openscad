#ifndef PREFERENCES_H_
#define PREFERENCES_H_

#include "qtgettext.h"
#include <QMainWindow>
#include <QSettings>
#include "ui_Preferences.h"
#include "rendersettings.h"
#include "linalg.h"
#include <map>

class Preferences : public QMainWindow, public Ui::Preferences
{
	Q_OBJECT;

public:
        // The values for the SYNTAX_HIGHLIGHT_* constants must match
        // the index of the entries in the preferences combobox.
        static const int SYNTAX_HIGHLIGHT_OFF;
        static const int SYNTAX_HIGHLIGHT_LIGHT_BG;
        static const int SYNTAX_HIGHLIGHT_DARK_BG;
        
        // The values for the COLOR_SCHEME_* constants must match
        // the index of the entries in the preferences listbox.
        static const int COLOR_SCHEME_CORNFIELD;
        static const int COLOR_SCHEME_METALLIC;
        static const int COLOR_SCHEME_SUNSET;
        
	~Preferences();
	static Preferences *inst() { if (!instance) instance = new Preferences(); return instance; }

	QVariant getValue(const QString &key) const;
	void apply() const;

public slots:
	void actionTriggered(class QAction *);
	void featuresCheckBoxToggled(bool);
	void on_colorSchemeChooser_itemSelectionChanged();
	void on_fontChooser_activated(const QString &);
	void on_fontSize_editTextChanged(const QString &);
	void on_syntaxHighlight_currentIndexChanged(const QString &);
	void on_openCSGWarningBox_toggled(bool);
	void on_enableOpenCSGBox_toggled(bool);
	void on_cgalCacheSizeEdit_textChanged(const QString &);
	void on_polysetCacheSizeEdit_textChanged(const QString &);
	void on_opencsgLimitEdit_textChanged(const QString &);
	void on_forceGoldfeatherBox_toggled(bool);
	void on_localizationCheckBox_toggled(bool);
	void on_updateCheckBox_toggled(bool);
	void on_snapshotCheckBox_toggled(bool);
	void on_checkNowButton_clicked();

signals:
	void requestRedraw() const;
	void fontChanged(const QString &family, uint size) const;
	void openCSGSettingsChanged() const;
	void syntaxHighlightChanged(const int idx);

private:
	Preferences(QWidget *parent = NULL);
	void keyPressEvent(QKeyEvent *e);
	void updateGUI();
	void removeDefaultSettings();
	void setupFeaturesPage();
	void addPrefPage(QActionGroup *group, QAction *action, QWidget *widget);

	QSettings::SettingsMap defaultmap;
	QHash<int, std::map<RenderSettings::RenderColor, Color4f> > colorschemes;
	QHash<const QAction *, QWidget *> prefPages;

	static Preferences *instance;
	static const char *featurePropertyName;
};

#endif
