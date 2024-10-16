/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <memory>
#include <string.h>

#include <com/sun/star/i18n/UnicodeType.hpp>
#include <com/sun/star/i18n/WordType.hpp>
#include <com/sun/star/i18n/XBreakIterator.hpp>

#include <unotools/charclass.hxx>

#include <hintids.hxx>
#include <doc.hxx>
#include <IDocumentUndoRedo.hxx>
#include <IDocumentContentOperations.hxx>
#include <ndtxt.hxx>
#include <txatbase.hxx>
#include <rubylist.hxx>
#include <pam.hxx>
#include <swundo.hxx>
#include <breakit.hxx>
#include <swcrsr.hxx>

using namespace ::com::sun::star::i18n;

/*
 * Members in the list:
 *   - String - the orig text
 *   - SwFormatRuby - the ruby attribute
 */
sal_uInt16 SwDoc::FillRubyList( const SwPaM& rPam, SwRubyList& rList )
{
    const SwPaM *_pStartCursor = rPam.GetNext(),
                *_pStartCursor2 = _pStartCursor;
    bool bCheckEmpty = &rPam != _pStartCursor;
    do {
        auto [pStt, pEnd] = _pStartCursor->StartEnd(); // SwPosition*
        if( !bCheckEmpty || ( pStt != pEnd && *pStt != *pEnd ))
        {
            SwPaM aPam( *pStt );
            do {
                std::unique_ptr<SwRubyListEntry> pNew(new SwRubyListEntry);
                if( pEnd != pStt )
                {
                    aPam.SetMark();
                    *aPam.GetMark() = *pEnd;
                }
                if( SelectNextRubyChars( aPam, *pNew ))
                {
                    rList.push_back(std::move(pNew));
                    aPam.DeleteMark();
                }
                else
                {
                     if( *aPam.GetPoint() < *pEnd )
                     {
                        // goto next paragraph
                        aPam.DeleteMark();
                        aPam.Move( fnMoveForward, GoInNode );
                     }
                     else
                        break;
                }
            } while( 30 > rList.size() && *aPam.GetPoint() < *pEnd );
        }
        if( 30 <= rList.size() )
            break;
        _pStartCursor = _pStartCursor->GetNext();
    } while( _pStartCursor != _pStartCursor2 );

    return rList.size();
}

void SwDoc::SetRubyList( const SwPaM& rPam, const SwRubyList& rList )
{
    GetIDocumentUndoRedo().StartUndo( SwUndoId::SETRUBYATTR, nullptr );
    const o3tl::sorted_vector<sal_uInt16> aDelArr{ RES_TXTATR_CJK_RUBY };

    SwRubyList::size_type nListEntry = 0;

    const SwPaM *_pStartCursor = rPam.GetNext(),
                *_pStartCursor2 = _pStartCursor;
    bool bCheckEmpty = &rPam != _pStartCursor;
    do {
        auto [pStt, pEnd] = _pStartCursor->StartEnd(); // SwPosition*
        if( !bCheckEmpty || ( pStt != pEnd && *pStt != *pEnd ))
        {

            SwPaM aPam( *pStt );
            do {
                SwRubyListEntry aCheckEntry;
                if( pEnd != pStt )
                {
                    aPam.SetMark();
                    *aPam.GetMark() = *pEnd;
                }
                if( SelectNextRubyChars( aPam, aCheckEntry ))
                {
                    const SwRubyListEntry* pEntry = rList[ nListEntry++ ].get();
                    if( aCheckEntry.GetRubyAttr() != pEntry->GetRubyAttr() )
                    {
                        // set/reset the attribute
                        if( !pEntry->GetRubyAttr().GetText().isEmpty() )
                        {
                            getIDocumentContentOperations().InsertPoolItem( aPam, pEntry->GetRubyAttr() );
                        }
                        else
                        {
                            ResetAttrs( aPam, true, aDelArr );
                        }
                    }

                    if (aCheckEntry.GetText() != pEntry->GetText())
                    {
                        if (pEntry->GetText().isEmpty())
                        {
                            ResetAttrs(aPam, true, aDelArr);
                        }

                        // text is changed, so replace the original
                        getIDocumentContentOperations().ReplaceRange(aPam, pEntry->GetText(),
                                                                     false);
                        std::swap(*aPam.GetMark(), *aPam.GetPoint());
                    }

                    aPam.DeleteMark();
                }
                else
                {
                     if( *aPam.GetPoint() < *pEnd )
                     {
                        // goto next paragraph
                        aPam.DeleteMark();
                        aPam.Move( fnMoveForward, GoInNode );
                     }
                     else
                    {
                        const SwRubyListEntry* pEntry = rList[ nListEntry++ ].get();

                        // set/reset the attribute
                        if( !pEntry->GetRubyAttr().GetText().isEmpty() &&
                            !pEntry->GetText().isEmpty() )
                        {
                            getIDocumentContentOperations().InsertString( aPam, pEntry->GetText() );
                            aPam.SetMark();
                            aPam.GetMark()->AdjustContent( -pEntry->GetText().getLength() );
                            getIDocumentContentOperations().InsertPoolItem(
                                aPam, pEntry->GetRubyAttr(), SetAttrMode::DONTEXPAND );
                        }
                        else
                            break;
                        aPam.DeleteMark();
                    }
                }
            } while( nListEntry < rList.size() && *aPam.GetPoint() < *pEnd );
        }
        if( 30 <= rList.size() )
            break;
        _pStartCursor = _pStartCursor->GetNext();
    } while( _pStartCursor != _pStartCursor2 );

    GetIDocumentUndoRedo().EndUndo( SwUndoId::SETRUBYATTR, nullptr );
}

bool SwDoc::SelectNextRubyChars( SwPaM& rPam, SwRubyListEntry& rEntry )
{
    // Point must be the startposition, Mark is optional the end position
    SwPosition* pPos = rPam.GetPoint();
    const SwTextNode* pTNd = pPos->GetNode().GetTextNode();
    OUString const& rText = pTNd->GetText();
    sal_Int32 nStart = pPos->GetContentIndex();
    sal_Int32 nEnd = rText.getLength();

    bool bHasMark = rPam.HasMark();
    if( bHasMark )
    {
        // in the same node?
        if( rPam.GetMark()->GetNode() == pPos->GetNode() )
        {
            // then use that end
            const sal_Int32 nTEnd = rPam.GetMark()->GetContentIndex();
            if( nTEnd < nEnd )
                nEnd = nTEnd;
        }
        rPam.DeleteMark();
    }

    // search the start
    // look where a ruby attribute starts
    const SwpHints* pHts = pTNd->GetpSwpHints();
    const SwTextAttr* pAttr = nullptr;
    if( pHts )
    {
        for( size_t nHtIdx = 0; nHtIdx < pHts->Count(); ++nHtIdx )
        {
            const SwTextAttr* pHt = pHts->Get(nHtIdx);
            if( RES_TXTATR_CJK_RUBY == pHt->Which() &&
                pHt->GetAnyEnd() > nStart )
            {
                if( pHt->GetStart() < nEnd )
                {
                    pAttr = pHt;
                    if( !bHasMark && nStart > pAttr->GetStart() )
                    {
                        nStart = pAttr->GetStart();
                        pPos->SetContent(nStart);
                    }
                }
                break;
            }
        }
    }

    if( !bHasMark && nStart && ( !pAttr || nStart != pAttr->GetStart()) )
    {
        // skip to the word begin!
        const sal_Int32 nWordStt = g_pBreakIt->GetBreakIter()->getWordBoundary(
                            rText, nStart,
                            g_pBreakIt->GetLocale( pTNd->GetLang( nStart )),
                            WordType::ANYWORD_IGNOREWHITESPACES,
                            true ).startPos;
        if (nWordStt < nStart && nWordStt >= 0)
        {
            nStart = nWordStt;
            pPos->SetContent(nStart);
        }
    }

    bool bAlphaNum = false;
    sal_Int32 nWordEnd = nEnd;
    CharClass& rCC = GetAppCharClass();
    while(  nStart < nEnd )
    {
        if( pAttr && nStart == pAttr->GetStart() )
        {
            pPos->SetContent(nStart);
            if( !rPam.HasMark() )
            {
                rPam.SetMark();
                pPos->SetContent(pAttr->GetAnyEnd());
                if( pPos->GetContentIndex() > nEnd )
                    pPos->SetContent(nEnd);
                rEntry.SetRubyAttr( pAttr->GetRuby() );
            }
            break;
        }

        sal_Int32 nChType = rCC.getType(rText, nStart);
        bool bIgnoreChar = false, bIsAlphaNum = false, bChkNxtWrd = false;
        switch( nChType )
        {
        case UnicodeType::UPPERCASE_LETTER:
        case UnicodeType::LOWERCASE_LETTER:
        case UnicodeType::TITLECASE_LETTER:
        case UnicodeType::DECIMAL_DIGIT_NUMBER:
                bChkNxtWrd = bIsAlphaNum = true;
                break;

        case UnicodeType::SPACE_SEPARATOR:
        case UnicodeType::CONTROL:
/*??*/  case UnicodeType::PRIVATE_USE:
        case UnicodeType::START_PUNCTUATION:
        case UnicodeType::END_PUNCTUATION:
            bIgnoreChar = true;
            break;

        case UnicodeType::OTHER_LETTER:
            bChkNxtWrd = true;
            [[fallthrough]];
        default:
                bIsAlphaNum = false;
                break;
        }

        if( rPam.HasMark() )
        {
            if( bIgnoreChar || bIsAlphaNum != bAlphaNum || nStart >= nWordEnd )
                break;
        }
        else if( !bIgnoreChar )
        {
            rPam.SetMark();
            bAlphaNum = bIsAlphaNum;
            if (bChkNxtWrd)
            {
                // search the end of this word
                nWordEnd = g_pBreakIt->GetBreakIter()->getWordBoundary(
                            rText, nStart,
                            g_pBreakIt->GetLocale( pTNd->GetLang( nStart )),
                            WordType::ANYWORD_IGNOREWHITESPACES,
                            true ).endPos;
                if( 0 > nWordEnd || nWordEnd > nEnd || nWordEnd == nStart )
                    nWordEnd = nEnd;
            }
        }
        pTNd->GoNext( pPos, SwCursorSkipMode::Chars );
        nStart = pPos->GetContentIndex();
    }

    nStart = rPam.GetMark()->GetContentIndex();
    rEntry.SetText( rText.copy( nStart,
                           rPam.GetPoint()->GetContentIndex() - nStart ));
    return rPam.HasMark();
}

SwRubyListEntry::~SwRubyListEntry()
{
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
