

URL=http://jbwcph002.silabs.com:8080/view/

for SDK in SDK6.8x SDK6.7x 
do
  for APP in serialapi_controller_bridge_ZW050x serialapi_controller_static_ZW050x
  do
    curl ${URL}/${SDK}/job/${SDK}_framework_build_all/lastSuccessfulBuild/artifact/ProductPlus/SerialAPIPlus/build/${APP}/Rels/${APP}_EU.map > ${SDK}_${APP}.map
  done
done
