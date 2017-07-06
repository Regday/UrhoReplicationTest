Library built:
Urho3D1.6 VS2015 AMD64 Release

Пометка:
Черный экран может быть по причине того, что рядом с бинарником не лежат CoreData и Data (используется UI/DefaultStyle.xml,Techniques/DiffUnlit.xml,"Models/Plane.mdl","Fonts/Anonymous Pro.ttf").
Проверялось на двух разных ПК, собирается и работает. Можно попробовать собрать с любой другой версией Urho, если поправить CMakeLists.txt >> set(ENV{URHO3D_HOME} ${CMAKE_SOURCE_DIR}/3rd).