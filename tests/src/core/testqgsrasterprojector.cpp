/***************************************************************************
                         testqgsrasterprojector.cpp
                         --------------------------
    begin                : July 2026
    copyright            : (C) 2026 by Gregory Lewis
    email                : gregory dot lewis at canada dot ca
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsapplication.h"
#include "qgscoordinatetransform.h"
#include "qgsrasterdataprovider.h"
#include "qgsrasterprojector.h"
#include "qgstest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

using namespace Qt::StringLiterals;

namespace
{
  class TestRasterProvider final : public QgsRasterDataProvider
  {
    public:
      TestRasterProvider( const QgsCoordinateReferenceSystem &crs, const QgsRectangle &extent )
        : mCrs( crs )
        , mExtent( extent )
      {}

      TestRasterProvider *clone() const override { return new TestRasterProvider( mCrs, mExtent ); }
      QgsCoordinateReferenceSystem crs() const override { return mCrs; }
      QgsRectangle extent() const override { return mExtent; }
      bool isValid() const override { return true; }
      QString name() const override { return u"test"_s; }
      QString description() const override { return u"Raster projector test provider"_s; }
      Qgis::DataType dataType( int bandNo ) const override
      {
        Q_UNUSED( bandNo )
        return Qgis::DataType::Byte;
      }
      Qgis::DataType sourceDataType( int bandNo ) const override
      {
        Q_UNUSED( bandNo )
        return Qgis::DataType::Byte;
      }
      int bandCount() const override { return 1; }
      QString lastErrorTitle() override { return QString(); }
      QString lastError() override { return QString(); }
      Qgis::RasterProviderCapabilities providerCapabilities() const override
      {
        return Qgis::RasterProviderCapability::ProviderHintCanPerformProviderResampling;
      }

      int requestedWidth() const { return mRequestedWidth; }
      int requestedHeight() const { return mRequestedHeight; }

    protected:
      using QgsRasterDataProvider::readBlock;
      bool readBlock( int bandNo, const QgsRectangle &viewExtent, int width, int height, void *data, QgsRasterBlockFeedback *feedback = nullptr ) override
      {
        Q_UNUSED( bandNo )
        Q_UNUSED( viewExtent )
        Q_UNUSED( feedback )
        mRequestedWidth = width;
        mRequestedHeight = height;
        std::fill_n( static_cast<unsigned char *>( data ), static_cast<size_t>( width ) * height, 1 );
        return true;
      }

    private:
      QgsCoordinateReferenceSystem mCrs;
      QgsRectangle mExtent;
      int mRequestedWidth = 0;
      int mRequestedHeight = 0;
  };

  struct LocalScale
  {
    QgsRectangle extent;
    double x = std::numeric_limits<double>::max();
    double y = std::numeric_limits<double>::max();
  };

  LocalScale minimumLocalScale( const QgsCoordinateTransform &ct, const QgsRectangle &sourceExtent, int sourceWidth, int sourceHeight )
  {
    QgsCoordinateTransform extentTransform = ct;
    extentTransform.setBallparkTransformsAreAppropriate( true );

    LocalScale result;
    result.extent = extentTransform.transformBoundingBox( sourceExtent );

    constexpr int steps = 3;
    const double sourceXResolution = sourceExtent.width() / sourceWidth;
    const double sourceYResolution = sourceExtent.height() / sourceHeight;
    const double sourceXStep = sourceExtent.width() / steps;
    const double sourceYStep = sourceExtent.height() / steps;

    for ( int column = 0; column < steps; ++column )
    {
      const double x = sourceExtent.xMinimum() + column * sourceXStep;
      for ( int row = 0; row < steps; ++row )
      {
        const double y = sourceExtent.yMinimum() + row * sourceYStep;
        const QgsRectangle pixelExtent( x - sourceXResolution / 2, y - sourceYResolution / 2, x + sourceXResolution / 2, y + sourceYResolution / 2 );
        try
        {
          const QgsRectangle destinationPixelExtent = extentTransform.transformBoundingBox( pixelExtent );
          if ( std::isfinite( destinationPixelExtent.width() ) && destinationPixelExtent.width() > 0 )
            result.x = std::min( result.x, destinationPixelExtent.width() );
          if ( std::isfinite( destinationPixelExtent.height() ) && destinationPixelExtent.height() > 0 )
            result.y = std::min( result.y, destinationPixelExtent.height() );
        }
        catch ( QgsCsException & )
        {
        }
      }
    }
    return result;
  }
}

class TestQgsRasterProjector : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void minimumLocalScale_data();
    void minimumLocalScale();
    void datelineSourceGrid_data();
    void datelineSourceGrid();
    void symmetricWebMercatorExtent_data();
    void symmetricWebMercatorExtent();
};

void TestQgsRasterProjector::initTestCase()
{
  QgsApplication::init();
  QgsApplication::initQgis();
}

void TestQgsRasterProjector::cleanupTestCase()
{
  QgsApplication::exitQgis();
}

void TestQgsRasterProjector::minimumLocalScale_data()
{
  QTest::addColumn<int>( "destinationEpsg" );
  QTest::addColumn<QgsRectangle>( "sourceExtent" );

  constexpr double pdcDatelineX = 3339584.7;
  QTest::newRow( "epsg3832-dateline" ) << 3832 << QgsRectangle( pdcDatelineX, -2100000, pdcDatelineX + 200000, -1900000 );
  QTest::newRow( "epsg3832-neighbor" ) << 3832 << QgsRectangle( pdcDatelineX + 100, -2100000, pdcDatelineX + 200100, -1900000 );
  QTest::newRow( "epsg3832-next-zoom" ) << 3832 << QgsRectangle( pdcDatelineX, -2050000, pdcDatelineX + 100000, -1950000 );
  QTest::newRow( "epsg5937" ) << 5937 << QgsRectangle( -7500000, -3000000, 2500000, 7000000 );
  QTest::newRow( "epsg5937-neighbor" ) << 5937 << QgsRectangle( -7480000, -2980000, 2520000, 7020000 );
  QTest::newRow( "epsg3573" ) << 3573 << QgsRectangle( -3000000, -3000000, 3000000, 3000000 );
  QTest::newRow( "epsg3573-neighbor" ) << 3573 << QgsRectangle( -2980000, -2980000, 3020000, 3020000 );
}

void TestQgsRasterProjector::minimumLocalScale()
{
  QFETCH( int, destinationEpsg );
  QFETCH( QgsRectangle, sourceExtent );

  const QgsCoordinateTransform transform(
    QgsCoordinateReferenceSystem::fromEpsgId( destinationEpsg ),
    QgsCoordinateReferenceSystem::fromEpsgId( 3857 ),
    QgsCoordinateTransformContext()
  );
  QVERIFY( transform.isValid() );

  constexpr int sourceWidth = 512;
  constexpr int sourceHeight = 512;
  const LocalScale expected = ::minimumLocalScale( transform, sourceExtent, sourceWidth, sourceHeight );
  QVERIFY( std::isfinite( expected.x ) );
  QVERIFY( std::isfinite( expected.y ) );

  QgsRectangle destinationExtent;
  int destinationWidth = 0;
  int destinationHeight = 0;
  QVERIFY( QgsRasterProjector::extentSize( transform, sourceExtent, sourceWidth, sourceHeight, destinationExtent, destinationWidth, destinationHeight ) );

  QCOMPARE( destinationExtent, expected.extent );
  QCOMPARE( destinationWidth, std::max( 1, static_cast<int>( expected.extent.width() / expected.x ) ) );
  QCOMPARE( destinationHeight, std::max( 1, static_cast<int>( expected.extent.height() / expected.y ) ) );
}

void TestQgsRasterProjector::datelineSourceGrid_data()
{
  QTest::addColumn<QgsRasterProjector::Precision>( "precision" );
  QTest::newRow( "approximate" ) << QgsRasterProjector::Approximate;
  QTest::newRow( "exact" ) << QgsRasterProjector::Exact;
}

void TestQgsRasterProjector::datelineSourceGrid()
{
  QFETCH( QgsRasterProjector::Precision, precision );

  const QgsCoordinateReferenceSystem sourceCrs = QgsCoordinateReferenceSystem::fromEpsgId( 3857 );
  const QgsCoordinateReferenceSystem destinationCrs = QgsCoordinateReferenceSystem::fromEpsgId( 3832 );
  TestRasterProvider provider( sourceCrs, QgsRectangle( -20037508.342789244, -20037508.342789244, 20037508.342789244, 20037508.342789244 ) );

  QgsRasterProjector projector;
  QVERIFY( projector.setInput( &provider ) );
  projector.setCrs( sourceCrs, destinationCrs, QgsCoordinateTransformContext() );
  projector.setPrecision( precision );

  constexpr int width = 512;
  constexpr int height = 512;
  constexpr double pdcDatelineX = 3339584.7;
  std::unique_ptr<QgsRasterBlock> block( projector.block( 1, QgsRectangle( pdcDatelineX, -2100000, pdcDatelineX + 200000, -1900000 ), width, height ) );

  QVERIFY( block );
  QVERIFY( block->isValid() );
  QVERIFY( !block->isEmpty() );
  QVERIFY( provider.requestedWidth() > 0 );
  QVERIFY( provider.requestedHeight() > 0 );
  QVERIFY( provider.requestedWidth() <= width * 10 );
  QVERIFY( provider.requestedHeight() <= height * 10 );
  QCOMPARE( provider.requestedWidth(), width * 10 );
}

void TestQgsRasterProjector::symmetricWebMercatorExtent_data()
{
  QTest::addColumn<QgsRasterProjector::Precision>( "precision" );
  QTest::newRow( "approximate" ) << QgsRasterProjector::Approximate;
  QTest::newRow( "exact" ) << QgsRasterProjector::Exact;
}

void TestQgsRasterProjector::symmetricWebMercatorExtent()
{
  QFETCH( QgsRasterProjector::Precision, precision );

  const QgsCoordinateReferenceSystem sourceCrs = QgsCoordinateReferenceSystem::fromEpsgId( 4326 );
  const QgsCoordinateReferenceSystem destinationCrs = QgsCoordinateReferenceSystem::fromEpsgId( 3857 );
  const QgsRectangle sourceExtent( -180, -85, 180, 85 );
  const QgsRectangle destinationExtent( -22000000, -22000000, 22000000, 22000000 );
  TestRasterProvider provider( sourceCrs, sourceExtent );

  QgsRasterProjector projector;
  QVERIFY( projector.setInput( &provider ) );
  projector.setCrs( sourceCrs, destinationCrs, QgsCoordinateTransformContext() );
  projector.setPrecision( precision );

  constexpr int width = 512;
  constexpr int height = 512;
  std::unique_ptr<QgsRasterBlock> block( projector.block( 1, destinationExtent, width, height ) );

  QVERIFY( block );
  QVERIFY( block->isValid() );
  QVERIFY( !block->isEmpty() );
  QVERIFY( provider.requestedWidth() > 0 );
  QVERIFY( provider.requestedHeight() > 0 );
  QVERIFY( provider.requestedWidth() <= width * 10 );
  QVERIFY( provider.requestedHeight() <= height * 10 );
}

QGSTEST_MAIN( TestQgsRasterProjector )
#include "testqgsrasterprojector.moc"