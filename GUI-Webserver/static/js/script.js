const myButton = document.getElementById('myButton');
const content = document.getElementById('content');

myButton.addEventListener('click', function() {
    content.textContent = 'Button clicked!';
});