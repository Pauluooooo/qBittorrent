/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2023  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (C) 2006  Christophe Dumez <chris@qbittorrent.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 */

#include "transferlistfilterswidget.h"

#include <QCheckBox>
#include <QIcon>
#include <QListWidgetItem>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QStyleOptionButton>
#include <QUrl>
#include <QVBoxLayout>

#include "base/algorithm.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrent.h"
#include "base/global.h"
#include "base/logger.h"
#include "base/net/downloadmanager.h"
#include "base/preferences.h"
#include "base/torrentfilter.h"
#include "base/utils/compare.h"
#include "base/utils/fs.h"
#include "transferlistfilters/categoryfilterwidget.h"
#include "transferlistfilters/statusfilterwidget.h"
#include "transferlistfilters/tagfilterwidget.h"
#include "transferlistfilters/trackersfilterwidget.h"
#include "transferlistwidget.h"
#include "uithememanager.h"
#include "utils.h"

namespace
{
    class ArrowCheckBox final : public QCheckBox
    {
    public:
        using QCheckBox::QCheckBox;

    private:
        void paintEvent(QPaintEvent *) override
        {
            QPainter painter(this);

            QStyleOptionViewItem indicatorOption;
            indicatorOption.initFrom(this);
            indicatorOption.rect = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &indicatorOption, this);
            indicatorOption.state |= (QStyle::State_Children
                                      | (isChecked() ? QStyle::State_Open : QStyle::State_None));
            style()->drawPrimitive(QStyle::PE_IndicatorBranch, &indicatorOption, &painter, this);

            QStyleOptionButton labelOption;
            initStyleOption(&labelOption);
            labelOption.rect = style()->subElementRect(QStyle::SE_CheckBoxContents, &labelOption, this);
            style()->drawControl(QStyle::CE_CheckBoxLabel, &labelOption, &painter, this);
        }
    };
}

TransferListFiltersWidget::TransferListFiltersWidget(QWidget *parent, TransferListWidget *transferList, const bool downloadFavicon)
    : QFrame(parent)
    , m_transferList(transferList)
{
    Preferences *const pref = Preferences::instance();

    // Construct lists
    auto *frame = new QFrame;
    auto *frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(0, 2, 0, 0);
    frameLayout->setSpacing(2);
    frameLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    QFont font;
    font.setBold(true);
    font.setCapitalization(QFont::AllUppercase);

    QCheckBox *statusLabel = new ArrowCheckBox(tr("Status"), this);
    statusLabel->setChecked(pref->getStatusFilterState());
    statusLabel->setFont(font);
    connect(statusLabel, &QCheckBox::toggled, pref, &Preferences::setStatusFilterState);
    frameLayout->addWidget(statusLabel);

    auto *statusFilters = new StatusFilterWidget(this, transferList);
    connect(statusLabel, &QCheckBox::toggled, statusFilters, &StatusFilterWidget::toggleFilter);
    frameLayout->addWidget(statusFilters);

    QCheckBox *categoryLabel = new ArrowCheckBox(tr("Categories"), this);
    categoryLabel->setChecked(pref->getCategoryFilterState());
    categoryLabel->setFont(font);
    connect(categoryLabel, &QCheckBox::toggled, this
            , &TransferListFiltersWidget::onCategoryFilterStateChanged);
    frameLayout->addWidget(categoryLabel);

    m_categoryFilterWidget = new CategoryFilterWidget(this);
    connect(m_categoryFilterWidget, &CategoryFilterWidget::actionDeleteTorrentsTriggered
            , transferList, &TransferListWidget::deleteVisibleTorrents);
    connect(m_categoryFilterWidget, &CategoryFilterWidget::actionPauseTorrentsTriggered
            , transferList, &TransferListWidget::pauseVisibleTorrents);
    connect(m_categoryFilterWidget, &CategoryFilterWidget::actionResumeTorrentsTriggered
            , transferList, &TransferListWidget::startVisibleTorrents);
    connect(m_categoryFilterWidget, &CategoryFilterWidget::categoryChanged
            , transferList, &TransferListWidget::applyCategoryFilter);
    toggleCategoryFilter(pref->getCategoryFilterState());
    frameLayout->addWidget(m_categoryFilterWidget);

    QCheckBox *tagsLabel = new ArrowCheckBox(tr("Tags"), this);
    tagsLabel->setChecked(pref->getTagFilterState());
    tagsLabel->setFont(font);
    connect(tagsLabel, &QCheckBox::toggled, this, &TransferListFiltersWidget::onTagFilterStateChanged);
    frameLayout->addWidget(tagsLabel);

    m_tagFilterWidget = new TagFilterWidget(this);
    connect(m_tagFilterWidget, &TagFilterWidget::actionDeleteTorrentsTriggered
            , transferList, &TransferListWidget::deleteVisibleTorrents);
    connect(m_tagFilterWidget, &TagFilterWidget::actionPauseTorrentsTriggered
            , transferList, &TransferListWidget::pauseVisibleTorrents);
    connect(m_tagFilterWidget, &TagFilterWidget::actionResumeTorrentsTriggered
            , transferList, &TransferListWidget::startVisibleTorrents);
    connect(m_tagFilterWidget, &TagFilterWidget::tagChanged
            , transferList, &TransferListWidget::applyTagFilter);
    toggleTagFilter(pref->getTagFilterState());
    frameLayout->addWidget(m_tagFilterWidget);

    QCheckBox *trackerLabel = new ArrowCheckBox(tr("Trackers"), this);
    trackerLabel->setChecked(pref->getTrackerFilterState());
    trackerLabel->setFont(font);
    connect(trackerLabel, &QCheckBox::toggled, pref, &Preferences::setTrackerFilterState);
    frameLayout->addWidget(trackerLabel);

    m_trackersFilterWidget = new TrackersFilterWidget(this, transferList, downloadFavicon);
    connect(trackerLabel, &QCheckBox::toggled, m_trackersFilterWidget, &TrackersFilterWidget::toggleFilter);
    frameLayout->addWidget(m_trackersFilterWidget);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(frame);

    auto *vLayout = new QVBoxLayout(this);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->addWidget(scroll);
}

void TransferListFiltersWidget::setDownloadTrackerFavicon(bool value)
{
    m_trackersFilterWidget->setDownloadTrackerFavicon(value);
}

void TransferListFiltersWidget::addTrackers(const BitTorrent::Torrent *torrent, const QVector<BitTorrent::TrackerEntry> &trackers)
{
    m_trackersFilterWidget->addTrackers(torrent, trackers);
}

void TransferListFiltersWidget::removeTrackers(const BitTorrent::Torrent *torrent, const QStringList &trackers)
{
    m_trackersFilterWidget->removeTrackers(torrent, trackers);
}

void TransferListFiltersWidget::refreshTrackers(const BitTorrent::Torrent *torrent)
{
    m_trackersFilterWidget->refreshTrackers(torrent);
}

void TransferListFiltersWidget::changeTrackerless(const BitTorrent::Torrent *torrent, const bool trackerless)
{
    m_trackersFilterWidget->changeTrackerless(torrent, trackerless);
}

void TransferListFiltersWidget::trackerEntriesUpdated(const BitTorrent::Torrent *torrent
        , const QHash<QString, BitTorrent::TrackerEntry> &updatedTrackerEntries)
{
    m_trackersFilterWidget->handleTrackerEntriesUpdated(torrent, updatedTrackerEntries);
}

void TransferListFiltersWidget::onCategoryFilterStateChanged(bool enabled)
{
    toggleCategoryFilter(enabled);
    Preferences::instance()->setCategoryFilterState(enabled);
}

void TransferListFiltersWidget::toggleCategoryFilter(bool enabled)
{
    m_categoryFilterWidget->setVisible(enabled);
    m_transferList->applyCategoryFilter(enabled ? m_categoryFilterWidget->currentCategory() : QString());
}

void TransferListFiltersWidget::onTagFilterStateChanged(bool enabled)
{
    toggleTagFilter(enabled);
    Preferences::instance()->setTagFilterState(enabled);
}

void TransferListFiltersWidget::toggleTagFilter(bool enabled)
{
    m_tagFilterWidget->setVisible(enabled);
    m_transferList->applyTagFilter(enabled ? m_tagFilterWidget->currentTag() : QString());
}
