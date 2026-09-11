/* bz-article-list-view.h
 *
 * Copyright 2026 Alexander Vanhee
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "bz-curated-articles-info.h"
#include <adwaita.h>

G_BEGIN_DECLS

#define BZ_TYPE_ARTICLE_LIST_VIEW (bz_article_list_view_get_type ())
G_DECLARE_FINAL_TYPE (BzArticleListView, bz_article_list_view, BZ, ARTICLE_LIST_VIEW, AdwBin)

GtkWidget *
bz_article_list_view_new (BzCuratedArticlesInfo *articles);

BzCuratedArticlesInfo *
bz_article_list_view_get_articles (BzArticleListView *self);

void
bz_article_list_view_set_articles (BzArticleListView     *self,
                                   BzCuratedArticlesInfo *articles);

G_END_DECLS
